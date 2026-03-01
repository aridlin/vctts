#include "app_state.h"
#include "win32_window.h"
#include "d3d11_renderer.h"
#include "imgui_ui.h"
#include "keyboard_hook.h"
#include "controller.h"
#include "audio_devices.h"
#include "audio_playback.h"
#include "tts_keyless.h"
#include "tts_winrt.h"
#include <windows.h>
#include <objbase.h>
#include <thread>
#include <vector>
#include <string>

// Include ImGui backend header (where WndProcHandler normally lives)
#include "imgui_impl_win32.h"

// Some ImGui forks/versions don't expose the symbol in the header depending on macros.
// This forward-declare makes compilation deterministic.
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static D3D11Renderer* g_renderer = nullptr;
static Controller* g_ctrl = nullptr;
static AppState*   g_state = nullptr;
static std::wstring SanitizeForSapi(const std::wstring& in)
{
    std::wstring out;
    for (wchar_t c : in) {
        if (c < 0x20 && c != L' ' && c != L'\n') continue;
        if (c >= 0xD800 && c <= 0xDFFF) continue;
        out.push_back(c);
    }
    return out;
}
static std::string WideToUtf8(const std::wstring& s)
{
    if (s.empty()) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
    std::string out(len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len, nullptr, nullptr);
    return out;
}

static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
    return out;
}

static std::vector<std::uint8_t> SpeakCustomCommand(const std::wstring& text, const char* cmdTemplate)
{
    std::vector<std::uint8_t> out;
    if (!cmdTemplate || cmdTemplate[0] == '\0') return out;

    std::string templ = cmdTemplate;
    std::string u8Text = WideToUtf8(text);

    size_t pos = templ.find("{text}");
    if (pos != std::string::npos)
    {
        // Escape quotes in the text to prevent breaking the command line
        std::string escapedText;
        escapedText.reserve(u8Text.size() + 10);
        for (char c : u8Text) {
            if (c == '"') escapedText += "\\\"";
            else escapedText += c;
        }
        templ.replace(pos, 6, escapedText);
    }

    std::wstring cmdW = Utf8ToWide(templ);

    HANDLE hReadPipe, hWritePipe;
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    sa.lpSecurityDescriptor = nullptr;

    if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
        return out;

    // Ensure the read handle is NOT inherited.
    SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof(si));
    si.cb = sizeof(si);
    si.hStdOutput = hWritePipe;
    // We don't want to capture stderr to the same buffer unless intended.
    // If they write errors to stdout, it will corrupt the WAV.
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(pi));

    // CreateProcessW requires a mutable string buffer
    std::vector<wchar_t> cmdBuf(cmdW.begin(), cmdW.end());
    cmdBuf.push_back(L'\0');

    if (CreateProcessW(
            nullptr,            // Application name
            cmdBuf.data(),      // Command line
            nullptr,            // Process attributes
            nullptr,            // Thread attributes
            TRUE,               // Inherit handles
            CREATE_NO_WINDOW,   // Creation flags
            nullptr,            // Environment
            nullptr,            // Current directory
            &si,
            &pi))
    {
        // Close write pipe on our side so we can read until EOF.
        CloseHandle(hWritePipe);

        DWORD read;
        char buffer[4096];
        while (ReadFile(hReadPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
        {
            out.insert(out.end(), buffer, buffer + read);
        }

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    else
    {
        CloseHandle(hWritePipe);
    }

    CloseHandle(hReadPipe);
    return out;
}

static std::vector<std::uint8_t> SpeakWithFallback(const std::wstring& text, bool preferKeyless)
{
    if (g_state &&
        g_state->sapiVoiceIndex >= 0 &&
        g_state->sapiVoiceIndex < (int)g_state->sapiVoices.size() &&
        g_state->sapiVoices[g_state->sapiVoiceIndex] == L"Custom")
    {
        return SpeakCustomCommand(text, g_state->customTtsCommand);
    }

    if (preferKeyless)
    {
        auto audio = tts_keyless::speak_to_audio_memory(text);
        if (!audio.empty()) return audio;
        return tts_winrt::speak_wav(text);
    }

    auto audio = tts_winrt::speak_wav(text);
    if (!audio.empty()) return audio;
    return tts_keyless::speak_to_audio_memory(text);
}

static void OnCommittedText(const std::wstring& text)
{
    if (!g_state || text.empty()) return;

    std::wstring copy = text;
    const bool preferKeyless = g_state->useKeylessBackup.load();
    std::thread([copy, preferKeyless]() {
        auto wav = SpeakWithFallback(SanitizeForSapi(copy), preferKeyless);
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
        keyboard_hook::poll_modifiers(state);
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

                const bool preferKeyless = g_state->useKeylessBackup.load();
                auto audio = SpeakWithFallback(L"Test text to speech.", preferKeyless);

                wchar_t buf[256];
                swprintf_s(buf, L"TTS bytes: %llu (preferKeyless=%s)",
                           (unsigned long long)audio.size(),
                           preferKeyless ? L"true" : L"false");
                OutputDebugStringW(L"[TTS] ");
                OutputDebugStringW(buf);
                OutputDebugStringW(L"\n");

                // Quick signature check for common failure modes
                if (!audio.empty())
                {
                    // MP3 often starts with "ID3" or 0xFF 0xFB (frame sync)
                    const bool looksMp3 =
                        (audio.size() >= 3 && audio[0] == 'I' && audio[1] == 'D' && audio[2] == '3') ||
                        (audio.size() >= 2 && audio[0] == 0xFF && (audio[1] & 0xE0) == 0xE0);

                    // WAV starts with "RIFF"
                    const bool looksWav =
                        (audio.size() >= 4 && audio[0] == 'R' && audio[1] == 'I' && audio[2] == 'F' && audio[3] == 'F');

                    if (!looksWav && !looksMp3)
                        OutputDebugStringW(L"[TTS] Warning: audio does not look like WAV or MP3 (maybe HTML error response)\n");
                }

                if (audio.empty()) {
                    MessageBoxW(nullptr, L"TTS produced an invalid/empty audio buffer. Check DebugView output.", L"TTS Error", MB_ICONERROR);
                    return;
                }

                audio_playback::play_wav_to_selected_async(audio, *g_state);
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
