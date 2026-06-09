#pragma once
#include "app_state.h"

#include <cstdint>
#include <vector>

namespace audio_playback
{
    bool refresh_output_devices(AppState& s);
    bool refresh_audio_devices(AppState& s);

    void play_wav_to_device_async(
        const std::vector<std::uint8_t>& wav,
        int deviceIndex
    );

    void play_wav_to_selected_async(
        const std::vector<std::uint8_t>& wav,
        const AppState& s
    );

    void play_test_tone_async(const AppState& s);

    void stop_all();
}
