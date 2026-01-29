#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

// Tiny D3D11 wrapper used by Dear ImGui DX11 backend.
struct D3D11Renderer
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

    // Create device + swapchain + render target.
    bool init(HWND hwnd);

    // Must be called on WM_SIZE when not minimized.
    void resize(UINT width, UINT height);

    // Clear and present.
    void begin_frame(const float clear_rgba[4]);
    void end_frame();

    // Release everything.
    void shutdown();
};

