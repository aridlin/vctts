#include "d3d11_renderer.h"

static void create_rtv(D3D11Renderer& r)
{
    if (!r.swap || !r.device) return;
    ID3D11Texture2D* backBuffer = nullptr;
    if (SUCCEEDED(r.swap->GetBuffer(0, IID_PPV_ARGS(&backBuffer))) && backBuffer) {
        r.device->CreateRenderTargetView(backBuffer, nullptr, &r.rtv);
        backBuffer->Release();
    }
}

bool D3D11Renderer::init(HWND hwnd)
{
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hwnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT flags = 0;
#ifdef _DEBUG
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL flOut{};
    const D3D_FEATURE_LEVEL fls[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr,
        D3D_DRIVER_TYPE_HARDWARE,
        nullptr,
        flags,
        fls,
        2,
        D3D11_SDK_VERSION,
        &sd,
        &swap,
        &device,
        &flOut,
        &ctx
    );

    if (FAILED(hr) || !device || !ctx || !swap)
        return false;

    create_rtv(*this);
    return rtv != nullptr;
}

void D3D11Renderer::resize(UINT width, UINT height)
{
    if (!swap) return;
    if (width == 0 || height == 0) return;

    if (rtv) { rtv->Release(); rtv = nullptr; }
    swap->ResizeBuffers(0, width, height, DXGI_FORMAT_UNKNOWN, 0);
    create_rtv(*this);
}

void D3D11Renderer::begin_frame(const float clear_rgba[4])
{
    if (!ctx || !rtv) return;
    ctx->OMSetRenderTargets(1, &rtv, nullptr);
    ctx->ClearRenderTargetView(rtv, clear_rgba);
}

void D3D11Renderer::end_frame()
{
    if (!swap) return;
    swap->Present(1, 0);
}

void D3D11Renderer::shutdown()
{
    if (rtv)  { rtv->Release();  rtv = nullptr; }
    if (swap) { swap->Release(); swap = nullptr; }
    if (ctx)  { ctx->Release();  ctx = nullptr; }
    if (device){ device->Release(); device = nullptr; }
}

