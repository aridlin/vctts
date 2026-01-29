#pragma once
#include "app_state.h"

#include <cstdint>
#include <vector>

namespace audio_playback
{
    // Enumerate playback devices using miniaudio (WASAPI on Windows).
    // Fills s.outDevices + s.outDevicesUtf8 and clamps s.devA/s.devB.
    bool refresh_output_devices(AppState& s);

    // Play a WAV image (RIFF bytes) from memory to BOTH selected devices.
    // Uses miniaudio. Runs in a detached thread. Cancels any previous playback.
    void play_wav_to_selected_async(const std::vector<std::uint8_t>& wav, int devA, int devB);

    // Convenience overload: snapshots indices from AppState and forwards.
    inline void play_wav_to_selected_async(const std::vector<std::uint8_t>& wav, const AppState& s)
    {
        play_wav_to_selected_async(wav, s.devA, s.devB);
    }

    // Cancel any currently playing audio (best effort).
    void stop_all();
}

