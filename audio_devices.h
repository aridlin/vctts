#pragma once
#include "app_state.h"

#include <cstdint>
#include <vector>

namespace audio_playback
{
    // Enumerate playback devices using miniaudio (WASAPI on Windows).
    // Fills s.outDevices + s.outDevicesUtf8 and clamps s.devA/s.devB.
    // Returns true if any devices found.
    bool refresh_output_devices(AppState& s);

    // Play a WAV image (RIFF bytes) from memory to BOTH selected devices (s.devA and s.devB).
    // Runs in a detached thread. Cancels any previous playback.
    void play_wav_to_selected_async(const std::vector<std::uint8_t>& wav, const AppState& s);

    // Cancel any currently playing audio (best effort).
    void stop_all();
}

