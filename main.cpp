#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#include "app_state.h"
#include "win32_window.h"
#include "d3d11_renderer.h"
#include "imgui_ui.h"
#include "keyboard_hook.h"
#include "controller.h"
#include "audio_devices.h"
#include "audio_playback.h"
#include "tts_sapi.h"

#include <objbase.h>
#include <thread>



// We need access to the renderer inside the Win32 message handler:
static D3D11Renderer* g_renderer = nullptr;

// Globals used by C-style callbacks
static Controller* g_ctrl  = nullptr;
static AppState*   g_state = nullptr;

// Fire-and-forget TTS+play, but NEVER capture AppState& into a background thread.
static void SpeakAndPlayAsync(std::wstring text, int devA, int devB)
{
    if (text.empty()) return;

    std::thread([text = std::move(text), devA, devB]() mutable {
        auto wav = tts_sapi::speak_to_wav_memory(text);
        audio_playback::play_wav_to_selected_async(wav, devA, devB);
    }).detach();
}

static void OnCommittedText(const std::wstring& text)
{
    if (!g_state) return;

    // Snapshot device indices NOW (safe). Do not touch AppState inside background threads.
    const int devA = g_state->devA;
    const int devB = g_state->devB;

    SpeakAndPlayAsync(text, devA, devB);
}

static void HookToggle(AppState&) { if (g_ctrl) g_ctrl->toggle_recording(); }
static void HookStop(AppState&)   { if (g_ctrl) g_ctrl->stop_recording(); }
static void HookAppend(AppState&, const wchar_t* t, int n) { if (g_ctrl) g_ctrl->on_append_text(t, n); }
static void HookBackspace(AppState&) { if (g_ctrl) g_ctrl->on_backspace(); }
static void HookExit(AppState& s) { s.exitRequested.store(true); }

// Hook Win32 messages so ImGui gets input and renderer resizes on WM_SIZE.
static bool MainMsgHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& outResult)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
    {
        outResult = 1;
        return true;
    }

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

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    constexpr const wchar_t* kClassName = L"TTS_OVERLAY_IMGUI";
    constexpr const wchar_t* kTitle     = L"TTS Voice Typing";

    if (!win32_window::create(state, hInstance, kClassName, kTitle, 560, 320, 720, 360))
    {
        MessageBoxW(nullptr, L"Failed to create window.", L"Error", MB_ICONERROR);
        CoUninitialize();
        return 1;
    }

    D3D11Renderer renderer;
    if (!renderer.init(state.hwnd))
    {
        MessageBoxW(nullptr, L"Failed to init D3D11.", L"Error", MB_ICONERROR);
        win32_window::destroy(state, hInstance, kClassName);
        CoUninitialize();
        return 1;
    }

    g_renderer = &renderer;

    win32_window::set_msg_handler(MainMsgHandler);

    // Fill device list for config UI
    RefreshOutputDevices(state);

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

        ctrl.tick_timeout();

        if (!IsWindowVisible(state.hwnd))
        {
            Sleep(10);
            continue;
        }

        UiAction action = ui.draw(state);

        if (action == UiAction::Quit)
        {
            state.exitRequested.store(true);
        }
        else if (action == UiAction::StartFromConfig)
        {
            // After config: hide until hotkey toggles recording
            win32_window::hide(state);
        }
        else if (action == UiAction::StopRecording)
        {
            ctrl.stop_recording();
        }
        else if (action == UiAction::TestTts)
        {
            // Snapshot indices (no AppState* in thread)
            const int devA = state.devA;
            const int devB = state.devB;
            SpeakAndPlayAsync(L"Test text to speech.", devA, devB);
        }

        const float clear_rgba[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
        renderer.begin_frame(clear_rgba);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        renderer.end_frame();
    }

    ui.shutdown();
    keyboard_hook::uninstall();
    audio_playback::stop_all();

    renderer.shutdown();
    win32_window::destroy(state, hInstance, kClassName);

    CoUninitialize();
    return 0;
}

