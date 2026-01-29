#include "app_state.h"
#include "win32_window.h"
#include "d3d11_renderer.h"
#include "imgui_ui.h"

#include <windows.h>

// We need access to the renderer inside the Win32 message handler:
static D3D11Renderer* g_renderer = nullptr;

// Hook Win32 messages so ImGui gets input and renderer resizes on WM_SIZE.
static bool MainMsgHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& outResult)
{
    // Let ImGui backend process messages first.
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    {
        outResult = 1;
        return true;
    }

    // Handle resize for D3D11 swapchain.
    if (msg == WM_SIZE && g_renderer && wParam != SIZE_MINIMIZED)
    {
        const UINT w = (UINT)LOWORD(lParam);
        const UINT h = (UINT)HIWORD(lParam);
        g_renderer->resize(w, h);
        outResult = 0;
        return true;
    }

    return false;
}

int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int)
{
    AppState state;

    // COM init (needed for audio device enumeration later; safe even if unused now)
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    constexpr const wchar_t* kClassName = L"TTS_OVERLAY_IMGUI";
    constexpr const wchar_t* kTitle     = L"TTS Voice Typing";

    // Create Win32 window
    if (!win32_window::create(state, hInstance, kClassName, kTitle, 560, 320, 720, 360))
    {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    // Init D3D11
    D3D11Renderer renderer;
    if (!renderer.init(state.hwnd))
    {
        MessageBoxW(nullptr, L"Failed to init D3D11.", L"Error", MB_ICONERROR);
        win32_window::destroy(state, hInstance, kClassName);
        CoUninitialize();
        return 1;
    }

    g_renderer = &renderer;

    // Install message handler (ImGui + WM_SIZE -> renderer.resize)
    win32_window::set_msg_handler(MainMsgHandler);

    // Init ImGui UI
    ImGuiUi ui;
    ui.init(state.hwnd, renderer);

    // Show window at startup (config screen)
    win32_window::show(state);
    win32_window::set_topmost(state, true);

    // Main loop
    MSG msg{};
    while (!state.exitRequested.load())
    {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);

            if (msg.message == WM_QUIT)
                state.exitRequested.store(true);
        }
        if (state.exitRequested.load())
            break;

        // If window is hidden (after Start), keep CPU chill.
        if (!IsWindowVisible(state.hwnd))
        {
            Sleep(10);
            continue;
        }

        // UI (build draw list)
        UiAction action = ui.draw(state);

        // Execute UI actions
        if (action == UiAction::Quit)
        {
            state.exitRequested.store(true);
        }
        else if (action == UiAction::StartFromConfig)
        {
            // After config: hide until hotkey/hook toggles recording later
            win32_window::hide(state);
        }
        else if (action == UiAction::StopRecording)
        {
            // Placeholder: hook module will call real stop later.
            // For now, just stop flag and hide.
            state.recording.store(false);
            win32_window::hide(state);
        }

        // Render + present
        const float clear_rgba[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
        renderer.begin_frame(clear_rgba);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        renderer.end_frame();
    }

    // Cleanup
    ui.shutdown();
    renderer.shutdown();
    win32_window::destroy(state, hInstance, kClassName);

    CoUninitialize();
    return 0;
}

