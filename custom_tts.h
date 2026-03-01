#pragma once
#include <vector>
#include <cstdint>
#include <string>

namespace custom_tts
{
    std::vector<std::uint8_t> SpeakCustomCommand(const std::wstring& text, const char* cmdTemplate);
    std::string WideToUtf8(const std::wstring& s);
    std::wstring Utf8ToWide(const std::string& s);
}
