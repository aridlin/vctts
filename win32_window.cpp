#include "win32_window.h"
#include <windows.h>

namespace {
    Win32MsgHandler g_msgHandler = nullptr;
}

namespace win32_window
{
    void set_msg_handler(Win32MsgHandler handler) { g_msgHandler = handler; }

    static AppState* get_state(HWND hWnd)
    {
        return reinterpret_cast<AppState*>(GetWindowLongPtrW(hWnd, GWLP_USERDATA));
    }

    LRESULT CALLBACK wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (g_msgHandler) {
            LRESULT out = 0;
            if (g_msgHandler(hWnd, msg, wParam, lParam, out))
                return out;
        }

        AppState* s = get_state(hWnd);

        // Capture typed characters the CORRECT way (layout/dead keys handled by Windows)
        if (s && s->recording.load())
        {
            switch (msg)
            {
            case WM_KEYDOWN:
                if (wParam == VK_BACK) {
                    s->backspace();
                    return 0;
                }
                break;

            case WM_CHAR:
            {
                wchar_t ch = (wchar_t)wParam;

                // Ignore control chars except newline-ish if you want.
                if (ch == 0) return 0;
                if (ch == L'\b') { s->backspace(); return 0; }
                if (ch == L'\r' || ch == L'\n') return 0; // Enter is handled by hook -> stop

                // Accept normal printable chars + space
                if (ch >= 32) {
                    s->appendSpan(&ch, 1);
                }
                return 0;
            }
            default:
                break;
            }
        }

        switch (msg)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;

        case WM_SYSCOMMAND:
            if ((wParam & 0xfff0) == SC_KEYMENU)
                return 0;
            break;
        default:
            break;
        }

        return DefWindowProcW(hWnd, msg, wParam, lParam);
    }

    bool create(AppState& state,
                HINSTANCE hInstance,
                const wchar_t* className,
                const wchar_t* title,
                int x, int y, int w, int h)
    {
        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.style = CS_CLASSDC;
        wc.lpfnWndProc = wndproc;
        wc.hInstance = hInstance;
        wc.lpszClassName = className;

        if (!RegisterClassExW(&wc))
            return false;

        HWND hwnd = CreateWindowExW(
            WS_EX_APPWINDOW,
            className,
            title,
            WS_OVERLAPPEDWINDOW,
            x, y, w, h,
            nullptr, nullptr,
            hInstance,
            nullptr
        );

        if (!hwnd) {
            UnregisterClassW(className, hInstance);
            return false;
        }

        state.hwnd = hwnd;

        // Store AppState* for WM_CHAR capture
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)&state);

        return true;
    }

    void destroy(AppState& state, HINSTANCE hInstance, const wchar_t* className)
    {
        if (state.hwnd) {
            DestroyWindow(state.hwnd);
            state.hwnd = nullptr;
        }
        UnregisterClassW(className, hInstance);
    }

    void show(AppState& state)
    {
        if (!state.hwnd) return;
        ShowWindow(state.hwnd, SW_SHOW);
        UpdateWindow(state.hwnd);
    }

    void hide(AppState& state)
    {
        if (!state.hwnd) return;
        ShowWindow(state.hwnd, SW_HIDE);
    }

    void set_topmost(AppState& state, bool topmost)
    {
        if (!state.hwnd) return;
        SetWindowPos(
            state.hwnd,
            topmost ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE
        );
    }

    void save_foreground(AppState& state)
    {
        state.prevForeground = GetForegroundWindow();
    }

    void restore_foreground(AppState& state)
    {
        if (!state.prevForeground) return;
        SetForegroundWindow(state.prevForeground);
        SetActiveWindow(state.prevForeground);
        state.prevForeground = nullptr;
    }
    void force_foreground(HWND hwnd)
    {
        HWND fg = GetForegroundWindow();
        if (!fg || fg == hwnd) {
            SetForegroundWindow(hwnd);
            return;
        }

        DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
        DWORD myThread = GetCurrentThreadId();

        AttachThreadInput(fgThread, myThread, TRUE);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
        SetActiveWindow(hwnd);
        AttachThreadInput(fgThread, myThread, FALSE);
    }

}

