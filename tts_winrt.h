#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace tts_winrt
{
    // Returns list of installed voice display names
    std::vector<std::wstring> list_voices();

    // Select voice by index from list_voices()
    void set_voice_index(int index);

    // Speak text → WAV in memory (PCM 16-bit)
    std::vector<std::uint8_t> speak_wav(const std::wstring& text);
    std::vector<std::uint8_t> speak_wav_with_language(
        const std::wstring& text,
        const std::string& language
    );
}
