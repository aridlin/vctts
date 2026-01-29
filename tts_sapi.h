#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace tts_sapi
{
    std::vector<std::uint8_t> speak_to_wav_memory(
        const std::wstring& text,
        int rate   = 0,
        int volume = 100
    );
}
