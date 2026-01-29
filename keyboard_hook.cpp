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

static void update_modifier(DWORD vk, bool down)
{
    if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT) g_shift.store(down);
    if (vk == VK_LCONTROL || vk == VK_RCONTROL || vk == VK_CONTROL) g_ctrl.store(down);
    if (vk == VK_LMENU || vk == VK_RMENU || vk == VK_MENU) g_alt.store(down);

    if (g_state) {
        g_state->shift.store(g_shift.load());
        g_state->ctrl.store(g_ctrl.load());
        g_state->alt.store(g_alt.load());
    }
}

static void sync_modifiers_from_async()
{
    const bool shiftDown = (GetAsyncKeyState(VK_SHIFT) & 0x8000) != 0;
    const bool ctrlDown = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    const bool altDown = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;

    if (g_shift.load() != shiftDown) g_shift.store(shiftDown);
    if (g_ctrl.load() != ctrlDown) g_ctrl.store(ctrlDown);
    if (g_alt.load() != altDown) g_alt.store(altDown);

    if (g_state) {
        g_state->shift.store(g_shift.load());
        g_state->ctrl.store(g_ctrl.load());
        g_state->alt.store(g_alt.load());
    }
}

static bool debounce_ok()
{
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - g_lastToggle).count();
    if (dt < g_toggleDebounceSec) return false;
    g_lastToggle = now;
    return true;
}

static void append_vk_as_text(DWORD vkCode, DWORD scanCode)
{
    if (!g_state || !g_cb.onAppendText) return;

    HKL layout = GetKeyboardLayout(0);

    BYTE ks[256]{};
    if (g_shift.load()) ks[VK_SHIFT] = 0x80;
    if (g_ctrl.load())  ks[VK_CONTROL] = 0x80;
    if (g_alt.load())   ks[VK_MENU] = 0x80;
    if ((GetKeyState(VK_CAPITAL) & 1) != 0) ks[VK_CAPITAL] = 0x01;

    wchar_t out[8]{};
    int rc = ToUnicodeEx((UINT)vkCode, (UINT)scanCode, ks, out, 8, 0, layout);
    if (rc > 0) {
        g_cb.onAppendText(*g_state, out, rc);
    }
}

static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode != HC_ACTION || !g_state)
        return CallNextHookEx(g_hook, nCode, wParam, lParam);

    const KBDLLHOOKSTRUCT* k = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lParam);
    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool up   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

    sync_modifiers_from_async();

    // Modifiers
    if (k->vkCode == VK_SHIFT || k->vkCode == VK_LSHIFT || k->vkCode == VK_RSHIFT ||
        k->vkCode == VK_CONTROL || k->vkCode == VK_LCONTROL || k->vkCode == VK_RCONTROL ||
        k->vkCode == VK_MENU || k->vkCode == VK_LMENU || k->vkCode == VK_RMENU)
    {
        if (down) update_modifier((DWORD)k->vkCode, true);
        if (up)   update_modifier((DWORD)k->vkCode, false);

        if (g_state->recording.load()) return 1;
        return CallNextHookEx(g_hook, nCode, wParam, lParam);
    }

    // Exit hotkey: Ctrl+Shift+Tab+E
    if (down && k->vkCode == 'E') {
        if (g_ctrl.load() && g_shift.load() && (GetAsyncKeyState(VK_TAB) & 0x8000)) {
            if (g_cb.onExit) g_cb.onExit(*g_state);
            return 1;
        }
    }

    // Toggle: Ctrl+Backspace
    if (down && k->vkCode == VK_BACK) {
        if (g_ctrl.load()) {
            if (g_state->configDone.load() && debounce_ok()) {
                if (g_cb.onToggleRecording) g_cb.onToggleRecording(*g_state);
            }
            return 1;
        }
    }

    // Recording mode: swallow + build buffer
    if (g_state->recording.load()) {
        if (down) {
            if (k->vkCode == VK_RETURN) {
                if (g_cb.onStopRecording) g_cb.onStopRecording(*g_state);
                return 1;
            }
            if (k->vkCode == VK_BACK) {
                if (g_cb.onBackspace) g_cb.onBackspace(*g_state);
                return 1;
            }
            if (k->vkCode == VK_SPACE) {
                static const wchar_t sp = L' ';
                if (g_cb.onAppendText) g_cb.onAppendText(*g_state, &sp, 1);
                return 1;
            }

            append_vk_as_text((DWORD)k->vkCode, (DWORD)k->scanCode);
            return 1;
        }
        return 1;
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
        g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
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
}
