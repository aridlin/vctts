#pragma once
#include "app_state.h"
#include <windows.h>

using Win32MsgHandler = bool(*)(HWND, UINT, WPARAM, LPARAM, LRESULT& outResult);

namespace win32_window
{
    bool create(AppState& state,
                HINSTANCE hInstance,
                const wchar_t* className,
                const wchar_t* title,
                int x, int y, int w, int h);

    void destroy(AppState& state, HINSTANCE hInstance, const wchar_t* className);

    void show(AppState& state);
    void hide(AppState& state);
    void set_topmost(AppState& state, bool topmost);

    void save_foreground(AppState& state);
    void restore_foreground(AppState& state);

    void set_msg_handler(Win32MsgHandler handler);

    LRESULT CALLBACK wndproc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void force_foreground(HWND hwnd);

}

