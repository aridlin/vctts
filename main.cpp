#include "app_state.h"
#include "win32_window.h"
#include "d3d11_renderer.h"
#include "imgui_ui.h"
#include "keyboard_hook.h"
#include "controller.h"
#include "audio_devices.h"
#include "audio_playback.h"
#include "tts_sapi.h"
#include "tts_keyless.h"

#include <windows.h>
#include <objbase.h>
#include <thread>

// Include ImGui backend header (where WndProcHandler normally lives)
#include "imgui_impl_win32.h"

// Some ImGui forks/versions don't expose the symbol in the header depending on macros.
// This forward-declare makes compilation deterministic.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static D3D11Renderer* g_renderer = nullptr;
static Controller* g_ctrl = nullptr;
static AppState*   g_state = nullptr;

static std::vector<std::uint8_t> SpeakWithFallback(const std::wstring& text, bool preferKeyless)
{
    if (preferKeyless)
    {
        auto audio = tts_keyless::speak_to_audio_memory(text);
        if (!audio.empty()) return audio;
        return tts_sapi::speak_to_wav_memory(text);
    }

    auto audio = tts_sapi::speak_to_wav_memory(text);
    if (!audio.empty()) return audio;
    return tts_keyless::speak_to_audio_memory(text);
}

static void OnCommittedText(const std::wstring& text)
{
    if (!g_state || text.empty()) return;

    std::wstring copy = text;
    const bool preferKeyless = g_state->useKeylessBackup.load();
    std::thread([copy, preferKeyless]() {
        auto wav = SpeakWithFallback(copy, preferKeyless);
        if (wav.empty()) {
            MessageBoxW(nullptr, L"TTS produced an invalid/empty audio buffer.", L"TTS Error", MB_ICONERROR);
            return;
        }
        audio_playback::play_wav_to_selected_async(wav, *g_state);
    }).detach();
}

static void HookToggle(AppState&) { if (g_ctrl) g_ctrl->toggle_recording(); }
static void HookStop(AppState&)   { if (g_ctrl) g_ctrl->stop_recording(); }
static void HookAppend(AppState&, const wchar_t* t, int n) { if (g_ctrl) g_ctrl->on_append_text(t, n); }
static void HookBackspace(AppState&) { if (g_ctrl) g_ctrl->on_backspace(); }
static void HookExit(AppState& s) { s.exitRequested.store(true); }

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

    RefreshOutputDevices(state);

    ImGuiUi ui;
    ui.init(state.hwnd, renderer);

    win32_window::show(state);
    win32_window::set_topmost(state, true);

    ControllerCallbacks cbs{};
    cbs.onCommittedText = &OnCommittedText;
    Controller ctrl(state, cbs);
    g_ctrl = &ctrl;

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
            win32_window::hide(state);
        }
        else if (action == UiAction::StopRecording)
        {
            ctrl.stop_recording();
        }
        else if (action == UiAction::TestTone)
        {
            audio_playback::play_test_tone_async(state);
        }
        else if (action == UiAction::TestTts)
        {
            std::thread([]{
                if (!g_state) return;
                auto wav = SpeakWithFallback(L"Test text to speech.", g_state->useKeylessBackup.load());
                if (wav.empty()) {
                    MessageBoxW(nullptr, L"TTS produced an invalid/empty audio buffer.", L"TTS Error", MB_ICONERROR);
                    return;
                }
                audio_playback::play_wav_to_selected_async(wav, *g_state);
            }).detach();
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
