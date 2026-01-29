#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace tts_sapi
{
    // Generate a complete WAV file in memory (RIFF header + PCM data).
    // Returns empty vector on failure or empty text.
    std::vector<uint8_t> speak_to_wav_memory(
        const std::wstring& text,
        int sampleRate = 22050,
        int channels   = 1,
        int bits       = 16,
        int rate       = 0,     // SAPI rate: typically -10..+10
        int volume     = 100    // 0..100
    );
}

