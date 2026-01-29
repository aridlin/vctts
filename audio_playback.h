#pragma once
#include "app_state.h"

#include <cstdint>
#include <vector>

namespace audio_playback
{
    bool refresh_output_devices(AppState& s);

    void play_wav_to_selected_async(
        const std::vector<std::uint8_t>& wav,
        const AppState& s
    );

    void stop_all();
}

