#pragma once

#include "app_state.h"
#include "d3d11_renderer.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// A small action enum so UI can request app operations without doing them itself.
enum class UiAction
{
    None,
    Quit,
    StartFromConfig,   // config done => hide window, enable hotkeys
    StopRecording,     // user pressed Stop/Speak
    TestTts,           // user pressed Test TTS in config
};

struct ImGuiUi
{
    void init(HWND hwnd, D3D11Renderer& r);
    void shutdown();

    // Draw one frame of UI and return requested action.
    UiAction draw(AppState& s);

    UiAction draw_config(AppState& s);
    UiAction draw_recording(AppState& s);
};

