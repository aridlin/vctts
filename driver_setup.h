#pragma once

#include "app_state.h"

#include <string>
#include <vector>

namespace driver_setup
{
    struct Manifest
    {
        std::wstring version;
        std::wstring infPath;
        std::vector<std::wstring> playbackMatches;
        std::vector<std::wstring> captureMatches;
    };

    bool parse_manifest_text(const std::wstring& text, Manifest& manifest);
    bool load_manifest(Manifest& manifest);
    DriverSetupStatus refresh_status(AppState& state);
    bool ensure_driver_package(HWND owner);
    bool install_or_repair(HWND owner);
    bool set_default_communications_capture(const AppState& state, HWND owner);
    int find_matching_device(const std::vector<AudioDevice>& devices, const std::vector<std::wstring>& matches);
}
