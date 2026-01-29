#pragma once
#include "app_state.h"
#include "d3d11_renderer.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

enum class UiAction
{
    None,
    Quit,
    StartFromConfig,
    StopRecording,
    TestTts,
    TestTone
};

struct ImGuiUi
{
    void init(HWND hwnd, D3D11Renderer& r);
    void shutdown();
    UiAction draw(AppState& s);

    UiAction draw_config(AppState& s);
    UiAction draw_recording(AppState& s);
};

