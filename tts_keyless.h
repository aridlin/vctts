#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace tts_keyless
{
    std::vector<std::uint8_t> speak_to_audio_memory(
        const std::wstring& text,
        const std::wstring& voice = L"Brian"
    );
}
