#pragma once
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>

struct D3D11Renderer
{
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    IDXGISwapChain* swap = nullptr;
    ID3D11RenderTargetView* rtv = nullptr;

    bool init(HWND hwnd);
    void resize(UINT width, UINT height);
    void begin_frame(const float clear_rgba[4]);
    void end_frame();
    void shutdown();
};

