#include "app_state.h"
#include "win32_window.h"
#include "d3d11_renderer.h"
#include "imgui_ui.h"
#include "keyboard_hook.h"
#include "controller.h"
#include "audio_devices.h"
#include "audio_playback.h"
#include "tts_sapi.h"
#include <windows.h>
#include <objbase.h>
#include <thread>
 
// We need access to the renderer inside the Win32 message handler:
static D3D11Renderer* g_renderer = nullptr;
// Globals used by C-style callbacks
static Controller* g_ctrl = nullptr;
static AppState*   g_state = nullptr;

static void OnCommittedText(const std::wstring& text)
{
    if (!g_state || text.empty()) return;
    auto wav = tts_sapi::speak_to_wav_memory(text);
    audio_playback::play_wav_to_selected_async(wav, *g_state);
}

static void HookToggle(AppState&) { if (g_ctrl) g_ctrl->toggle_recording(); }
static void HookStop(AppState&) { if (g_ctrl) g_ctrl->stop_recording(); }
static void HookAppend(AppState&, const wchar_t* t, int n) { if (g_ctrl) g_ctrl->on_append_text(t, n); }
static void HookBackspace(AppState&) { if (g_ctrl) g_ctrl->on_backspace(); }
static void HookExit(AppState& s) { s.exitRequested.store(true); }
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
    g_state = &state;

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
    
    // Fill device list for config UI
    RefreshOutputDevices(state);

    // Init ImGui UI
    ImGuiUi ui;
    ui.init(state.hwnd, renderer);

    // Show window at startup (config screen)
    win32_window::show(state);
    win32_window::set_topmost(state, true);
    // Controller: show/hide/focus + commit callback
    ControllerCallbacks cbs{};
    cbs.onCommittedText = &OnCommittedText;
    Controller ctrl(state, cbs);
    g_ctrl = &ctrl;

    // Global keyboard hook
    HookCallbacks hcb{};
    hcb.onToggleRecording = &HookToggle;
    hcb.onStopRecording   = &HookStop;
    hcb.onAppendText      = &HookAppend;
    hcb.onBackspace       = &HookBackspace;
    hcb.onExit            = &HookExit;

    if (!keyboard_hook::install(state, hcb))
    {
        MessageBoxW(nullptr, L"Failed to install keyboard hook.", L"Error", MB_ICONERROR);
        state.exitRequested.store(true);
    }

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
        // Timeout enforcement while recording
        ctrl.tick_timeout();
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
            ctrl.stop_recording();
        }
        else if (action == UiAction::TestTts)
        {
            std::thread([]{
                if (!g_state) return;
                auto wav = tts_sapi::speak_to_wav_memory(L"Test text to speech.");
                audio_playback::play_wav_to_selected_async(wav, *g_state);
            }).detach();
        }

        // Render + present
        const float clear_rgba[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
        renderer.begin_frame(clear_rgba);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        renderer.end_frame();
    }

    // Cleanup
    ui.shutdown();
    keyboard_hook::uninstall();
    audio_playback::stop_all();
    renderer.shutdown();
    win32_window::destroy(state, hInstance, kClassName);

    CoUninitialize();
    return 0;
}

