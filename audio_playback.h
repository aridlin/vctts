#pragma once
#include "app_state.h"

#include <cstdint>
#include <vector>

namespace audio_playback
{
    // Enumerate playback devices (WASAPI via miniaudio). Fills AppState device lists.
    bool refresh_output_devices(AppState& s);

    // Play WAV RIFF bytes to BOTH selected devices (devA/devB) asynchronously.
    void play_wav_to_selected_async(const std::vector<std::uint8_t>& wav, const AppState& s);

    // Cancel any current playback (best effort).
    void stop_all();
}

