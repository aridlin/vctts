#pragma once
#include "app_state.h"

#include <windows.h>

// Small callback interface for hook events.
// main.cpp (or a controller module) implements these.
struct HookCallbacks
{
    // Toggle recording (Ctrl+Backspace)
    void (*onToggleRecording)(AppState& s) = nullptr;

    // Stop recording (Enter)
    void (*onStopRecording)(AppState& s) = nullptr;

    // Append a Unicode character (produced by ToUnicodeEx)
    void (*onAppendText)(AppState& s, const wchar_t* text, int count) = nullptr;

    // Backspace
    void (*onBackspace)(AppState& s) = nullptr;

    // Exit request (Ctrl+Shift+Tab+E)
    void (*onExit)(AppState& s) = nullptr;
};

namespace keyboard_hook
{
    // Install global low-level keyboard hook.
    // Returns true on success.
    bool install(AppState& s, const HookCallbacks& cb);

    // Uninstall hook if installed.
    void uninstall();

    // (Optional) Check if installed.
    bool is_installed();
}

