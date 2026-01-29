#include "imgui_ui.h"
#include "audio_devices.h"

void ImGuiUi::init(HWND hwnd, D3D11Renderer& r)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(r.device, r.ctx);
}

void ImGuiUi::shutdown()
{
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
}

UiAction ImGuiUi::draw(AppState& s)
{
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();

    UiAction action = UiAction::None;

    if (!s.configDone.load())
        action = draw_config(s);
    else
        action = draw_recording(s);

    ImGui::Render();
    return action;
}

UiAction ImGuiUi::draw_config(AppState& s)
{
    ImGui::SetNextWindowSize(ImVec2(720, 380), ImGuiCond_Always);
    ImGui::Begin("TTS Voice Typing - Config", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Quit")) {
        ImGui::End();
        return UiAction::Quit;
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Startup config");
    ImGui::TextDisabled("Pick TWO output devices (miniaudio/WASAPI).");

    if (ImGui::Button("Refresh devices")) {
        RefreshOutputDevices(s);
    }

    ImGui::Spacing();

    if (s.outDevicesUtf8.empty()) {
        ImGui::TextColored(ImVec4(1, 0.6f, 0.6f, 1), "No output devices found.");
        ImGui::TextDisabled("Press Refresh devices.");
        ImGui::End();
        return UiAction::None;
    }

    if (s.devA < 0) s.devA = 0;
    if (s.devB < 0) s.devB = 0;
    if (s.devA >= (int)s.outDevicesUtf8.size()) s.devA = 0;
    if (s.devB >= (int)s.outDevicesUtf8.size()) s.devB = 0;

    ImGui::TextUnformatted("Device A:");
    if (ImGui::BeginCombo("##devA", s.outDevicesUtf8[s.devA].c_str())) {
        for (int i = 0; i < (int)s.outDevicesUtf8.size(); i++) {
            bool sel = (i == s.devA);
            if (ImGui::Selectable(s.outDevicesUtf8[i].c_str(), sel))
                s.devA = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::TextUnformatted("Device B:");
    if (ImGui::BeginCombo("##devB", s.outDevicesUtf8[s.devB].c_str())) {
        for (int i = 0; i < (int)s.outDevicesUtf8.size(); i++) {
            bool sel = (i == s.devB);
            if (ImGui::Selectable(s.outDevicesUtf8[i].c_str(), sel))
                s.devB = i;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::Separator();
    ImGui::TextDisabled("Hotkeys work after you press Start.");
    if (ImGui::Button("Start")) {
        s.configDone.store(true);
        ImGui::End();
        return UiAction::StartFromConfig;
    }

    ImGui::SameLine();
    if (ImGui::Button("Test Tone")) {
        ImGui::End();
        return UiAction::TestTone;
    }

    ImGui::SameLine();
    if (ImGui::Button("Test TTS")) {
        ImGui::End();
        return UiAction::TestTts;
    }

    ImGui::End();
    return UiAction::None;
}

UiAction ImGuiUi::draw_recording(AppState& s)
{
    ImGui::SetNextWindowSize(ImVec2(700, 320), ImGuiCond_Always);
    ImGui::Begin("Voice Typing", nullptr,
        ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

    if (ImGui::Button("Quit")) {
        ImGui::End();
        return UiAction::Quit;
    }

    ImGui::SameLine();
    ImGui::TextDisabled("Toggle: Ctrl+Backspace | Stop: Enter | Exit: Ctrl+Shift+Tab+E");

    ImGui::Separator();
    ImGui::TextDisabled("Recording: %s", s.recording.load() ? "YES" : "no");

    std::wstring copy = s.copyBuffer();
    std::string preview = AppState::sanitizePreview(copy);

    ImGui::TextUnformatted("Preview:");
    ImGui::BeginChild("##preview", ImVec2(0, 210), true);
    ImGui::TextUnformatted(preview.c_str());
    ImGui::EndChild();

    if (ImGui::Button("Stop & Speak")) {
        ImGui::End();
        return UiAction::StopRecording;
    }

    ImGui::End();
    return UiAction::None;
}
