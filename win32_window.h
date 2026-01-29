#pragma once

#include "app_state.h"
#include <windows.h>

// Optional message hook so other modules (ImGui backend, renderer) can intercept messages.
// If it returns true, the message is considered handled and WndProc returns outResult.
using Win32MsgHandler = bool(*)(HWND, UINT, WPARAM, LPARAM, LRESULT& outResult);

namespace win32_window
{
    // Registers class + creates window. Stores HWND into state.hwnd.
    // Returns false on failure.
    bool create(AppState& state,
                HINSTANCE hInstance,
                const wchar_t* className,
                const wchar_t* title,
                int x, int y, int w, int h);

    // Destroy window and unregister class.
    void destroy(AppState& state, HINSTANCE hInstance, const wchar_t* className);

    // Show/hide and topmost convenience.
    void show(AppState& state);
    void hide(AppState& state);
    void set_topmost(AppState& state, bool topmost);

    // Focus helpers (for your “steal focus then return” flow)
    void save_foreground(AppState& state);
    void restore_foreground(AppState& state);

    // Install a message handler hook (used to plug ImGui_ImplWin32_WndProcHandler)
    void set_msg_handler(Win32MsgHandler handler);

    // The window procedure (implemented in win32_window.cpp)
    LRESULT CALLBACK wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
}

