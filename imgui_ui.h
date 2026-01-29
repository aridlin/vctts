#pragma once

#include "app_state.h"
#include "d3d11_renderer.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include <thread>

// A small action enum so UI can request app operations without doing them itself.
enum class UiAction
{
    None,
    Quit,
    StartFromConfig,   // config done => hide window, enable hotkeys
    StopRecording,     // user pressed Stop/Speak
    TestTts,
};

struct ImGuiUi
{
    // Init/shutdown Dear ImGui. Call once.
    void init(HWND hwnd, D3D11Renderer& r);
    void shutdown();

    // Draw one frame of UI.
    // Returns an action request for main.cpp to process.
    UiAction draw(AppState& s);

    // Helpers for the two screens
    UiAction draw_config(AppState& s);
    UiAction draw_recording(AppState& s);
};

