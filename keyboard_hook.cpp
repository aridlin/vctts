#include "keyboard_hook.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <iterator>
#include <mutex>
#include <thread>

static HHOOK g_hook = nullptr;
static AppState* g_state = nullptr;
static HookCallbacks g_cb{};
static std::thread g_hookThread;
static DWORD g_hookThreadId = 0;
static std::mutex g_hookMutex;
static std::condition_variable g_hookCv;
static bool g_hookInstallDone = false;

static std::atomic<bool> g_shift{false};
static std::atomic<bool> g_ctrl{false};
static std::atomic<bool> g_alt{false};

static std::chrono::steady_clock::time_point g_lastToggle = std::chrono::steady_clock::now();
static double g_toggleDebounceSec = 0.35;
static bool g_recordingTracked = false;
static bool g_swallowedKeys[256]{};

static bool key_down_async(int vk)
{
    return (GetAsyncKeyState(vk) & 0x8000) != 0;
}

static bool ctrl_down()
{
    return key_down_async(VK_CONTROL) || key_down_async(VK_LCONTROL) || key_down_async(VK_RCONTROL);
}

static bool shift_down()
{
    return key_down_async(VK_SHIFT) || key_down_async(VK_LSHIFT) || key_down_async(VK_RSHIFT);
}

static bool alt_down()
{
    return key_down_async(VK_MENU) || key_down_async(VK_LMENU) || key_down_async(VK_RMENU);
}

static void push_modifiers_to_state(AppState& s)
{
    s.shift.store(g_shift.load());
    s.ctrl.store(g_ctrl.load());
    s.alt.store(g_alt.load());
}

static void sync_modifiers_from_async()
{
    g_shift.store(shift_down());
    g_ctrl.store(ctrl_down());
    g_alt.store(alt_down());

    if (g_state) push_modifiers_to_state(*g_state);
}

static void clear_swallowed_keys()
{
    std::memset(g_swallowedKeys, 0, sizeof(g_swallowedKeys));
}

static void update_recording_tracking(bool recording)
{
    if (recording == g_recordingTracked) return;
    if (recording)
        clear_swallowed_keys();
    g_recordingTracked = recording;
}

static bool debounce_ok()
{
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - g_lastToggle).count();
    if (dt < g_toggleDebounceSec) return false;
    g_lastToggle = now;
    return true;
}

static HKL foreground_layout()
{
    HWND fg = GetForegroundWindow();
    DWORD threadId = fg ? GetWindowThreadProcessId(fg, nullptr) : 0;
    HKL layout = threadId ? GetKeyboardLayout(threadId) : nullptr;
    return layout ? layout : GetKeyboardLayout(0);
}

static int translate_key_to_chars(const KBDLLHOOKSTRUCT& key, wchar_t* out, int outCount)
{
    if (!out || outCount <= 0 || key.vkCode >= 256)
        return 0;

    BYTE keyState[256]{};
    GetKeyboardState(keyState);

    keyState[VK_SHIFT] = shift_down() ? 0x80 : 0;
    keyState[VK_CONTROL] = ctrl_down() ? 0x80 : 0;
    keyState[VK_MENU] = alt_down() ? 0x80 : 0;
    keyState[key.vkCode] = 0x80;

    const UINT scan = key.scanCode;
    int count = ToUnicodeEx(key.vkCode, scan, keyState, out, outCount, 0, foreground_layout());
    if (count < 0)
        return 0;
    return count;
}

static void open_config(AppState& s)
{
    if (s.recording.load()) {
        if (g_cb.onCancelRecording) g_cb.onCancelRecording(s);
    }
    if (g_cb.onOpenConfig) g_cb.onOpenConfig(s);
}

static bool handle_global_hotkeys(AppState& s, DWORD vk)
{
    if (vk == VK_BACK) {
        if (ctrl_down() && shift_down()) {
            open_config(s);
            return true;
        }

        if (ctrl_down()) {
            if (s.configDone.load() && debounce_ok()) {
                if (g_cb.onToggleRecording) g_cb.onToggleRecording(s);
            }
            return true;
        }
    }

    if (vk == 'E' && ctrl_down() && shift_down() && key_down_async(VK_TAB)) {
        if (g_cb.onExit) g_cb.onExit(s);
        return true;
    }

    return false;
}

static bool handle_recording_key(AppState& s, const KBDLLHOOKSTRUCT& key)
{
    if (key.vkCode >= 256)
        return false;

    g_swallowedKeys[key.vkCode] = true;

    if (handle_global_hotkeys(s, key.vkCode))
        return true;

    if (key.vkCode == VK_RETURN) {
        if (g_cb.onStopRecording) g_cb.onStopRecording(s);
        return true;
    }

    if (key.vkCode == VK_ESCAPE) {
        if (g_cb.onCancelRecording) g_cb.onCancelRecording(s);
        return true;
    }

    if (key.vkCode == VK_BACK) {
        if (g_cb.onBackspace) g_cb.onBackspace(s);
        return true;
    }

    wchar_t text[8]{};
    int count = translate_key_to_chars(key, text, (int)std::size(text));
    if (count > 0 && g_cb.onAppendText) {
        wchar_t printable[8]{};
        int printableCount = 0;
        for (int i = 0; i < count && printableCount < (int)std::size(printable); ++i) {
            if (text[i] >= 32)
                printable[printableCount++] = text[i];
        }
        if (printableCount > 0)
            g_cb.onAppendText(s, printable, printableCount);
    }

    return true;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || !g_state)
        return CallNextHookEx(g_hook, nCode, wParam, lParam);

    const KBDLLHOOKSTRUCT* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool up = (wParam == WM_KEYUP || wParam == WM_SYSKEYUP);
    const bool recording = g_state->recording.load();

    update_recording_tracking(recording);

    if (!recording) {
        if (up && key->vkCode < 256 && g_swallowedKeys[key->vkCode]) {
            g_swallowedKeys[key->vkCode] = false;
            return 1;
        }
        if (down && handle_global_hotkeys(*g_state, key->vkCode)) {
            sync_modifiers_from_async();
            return 1;
        }
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    sync_modifiers_from_async();

    if (down && handle_recording_key(*g_state, *key))
        return 1;

    if (up && key->vkCode < 256 && g_swallowedKeys[key->vkCode]) {
        g_swallowedKeys[key->vkCode] = false;
        return 1;
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

static void hook_thread_main()
{
    g_hookThreadId = GetCurrentThreadId();
    MSG queueMsg{};
    PeekMessageW(&queueMsg, nullptr, WM_USER, WM_USER, PM_NOREMOVE);

    g_hook = SetWindowsHookExW(
        WH_KEYBOARD_LL,
        LowLevelKeyboardProc,
        GetModuleHandleW(nullptr),
        0
    );

    {
        std::lock_guard<std::mutex> lock(g_hookMutex);
        g_hookInstallDone = true;
    }
    g_hookCv.notify_all();

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (g_hook) {
        UnhookWindowsHookEx(g_hook);
        g_hook = nullptr;
    }
    g_hookThreadId = 0;
}

namespace keyboard_hook
{
    bool install(AppState& s, const HookCallbacks& cb)
    {
        if (g_hookThread.joinable())
            return g_hook != nullptr;

        g_state = &s;
        g_cb = cb;
        g_toggleDebounceSec = s.toggleDebounceSec;
        g_recordingTracked = s.recording.load();
        clear_swallowed_keys();
        g_hookInstallDone = false;

        g_hookThread = std::thread(hook_thread_main);

        std::unique_lock<std::mutex> lock(g_hookMutex);
        g_hookCv.wait_for(lock, std::chrono::seconds(2), [] { return g_hookInstallDone; });
        return g_hook != nullptr;
    }

    void uninstall()
    {
        if (g_hookThread.joinable()) {
            if (g_hookThreadId != 0)
                PostThreadMessageW(g_hookThreadId, WM_QUIT, 0, 0);
            g_hookThread.join();
        }
        g_state = nullptr;
        g_cb = HookCallbacks{};
        g_hookInstallDone = false;
        clear_swallowed_keys();
    }

    bool is_installed() { return g_hook != nullptr; }

    void poll_modifiers(AppState& s)
    {
        sync_modifiers_from_async();
        push_modifiers_to_state(s);
    }
}
