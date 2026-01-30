#include "keyboard_hook.h"
#include <atomic>
#include <chrono>

static HHOOK g_hook = nullptr;
static AppState* g_state = nullptr;
static HookCallbacks g_cb{};

static std::atomic<bool> g_shift{false};
static std::atomic<bool> g_ctrl{false};
static std::atomic<bool> g_alt{false};

static std::chrono::steady_clock::time_point g_lastToggle = std::chrono::steady_clock::now();
static double g_toggleDebounceSec = 0.35;

static void push_modifiers_to_state(AppState& s)
{
    s.shift.store(g_shift.load());
    s.ctrl.store(g_ctrl.load());
    s.alt.store(g_alt.load());
}

static void sync_modifiers_from_async()
{
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool altDown   = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    g_shift.store(shiftDown);
    g_ctrl.store(ctrlDown);
    g_alt.store(altDown);

    if (g_state) push_modifiers_to_state(*g_state);
}

static bool debounce_ok()
{
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - g_lastToggle).count();
    if (dt < g_toggleDebounceSec) return false;
    g_lastToggle = now;
    return true;
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || !g_state)
        return CallNextHookEx(g_hook, nCode, wParam, lParam);

    const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

    // Keep modifiers correct regardless of what we swallow.
    sync_modifiers_from_async();

    // Exit hotkey: Ctrl+Shift+Tab+E (swallow)
    if (down && k->vkCode == 'E') {
        if (g_ctrl.load() && g_shift.load() && (GetAsyncKeyState(VK_TAB) & 0x8000)) {
            if (g_cb.onExit) g_cb.onExit(*g_state);
            return 1;
        }
    }

    // Toggle: Ctrl+Backspace (swallow so target app/game doesn't backspace)
    if (down && k->vkCode == VK_BACK) {
        if (g_ctrl.load()) {
            if (g_state->configDone.load() && debounce_ok()) {
                if (g_cb.onToggleRecording) g_cb.onToggleRecording(*g_state);
            }
            return 1;
        }
    }

    // While recording: DO NOT swallow normal typing keys.
    // We want our overlay window to receive WM_CHAR / WM_KEYDOWN normally.
    if (g_state->recording.load())
    {
        // Optional: still intercept Enter to stop recording even if the overlay loses focus.
        if (down && k->vkCode == VK_RETURN) {
            if (g_cb.onStopRecording) g_cb.onStopRecording(*g_state);
            return 1;
        }

        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    return CallNextHookEx(g_hook, nCode, wParam, lParam);
}

namespace keyboard_hook
{
    bool install(AppState& s, const HookCallbacks& cb)
    {
        g_state = &s;
        g_cb = cb;
        g_toggleDebounceSec = s.toggleDebounceSec;

        g_hook = SetWindowsHookExW(
            WH_KEYBOARD_LL,
            LowLevelKeyboardProc,
            GetModuleHandleW(nullptr),
            0
        );

        return g_hook != nullptr;
    }

    void uninstall()
    {
        if (g_hook) {
            UnhookWindowsHookEx(g_hook);
            g_hook = nullptr;
        }
        g_state = nullptr;
        g_cb = HookCallbacks{};
    }

    bool is_installed() { return g_hook != nullptr; }

    void poll_modifiers(AppState& s)
    {
        const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool ctrlDown  = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
        const bool altDown   = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

        g_shift.store(shiftDown);
        g_ctrl.store(ctrlDown);
        g_alt.store(altDown);

        push_modifiers_to_state(s);
    }
}

