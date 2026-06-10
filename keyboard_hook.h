#pragma once
#include "app_state.h"
#include <windows.h>

struct HookCallbacks
{
    void (*onToggleRecording)(AppState& s) = nullptr;
    void (*onStopRecording)(AppState& s) = nullptr;
    void (*onAppendText)(AppState& s, const wchar_t* text, int count) = nullptr;
    void (*onBackspace)(AppState& s) = nullptr;
    void (*onCancelRecording)(AppState& s) = nullptr;
    void (*onOpenConfig)(AppState& s) = nullptr;
    void (*onExit)(AppState& s) = nullptr;
};

namespace keyboard_hook
{
    bool install(AppState& s, const HookCallbacks& cb);
    void uninstall();
    bool is_installed();

    // Call this each frame to prevent "stuck Ctrl/Shift/Alt" in AppState.
    void poll_modifiers(AppState& s);
}
