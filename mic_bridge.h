#pragma once

#include "app_state.h"

#include <cstdint>
#include <vector>

namespace mic_bridge
{
    bool sync(AppState& state);
    bool submit_tts(const std::vector<std::uint8_t>& audioBytes, AppState& state);
    void stop();
    bool running();
}
