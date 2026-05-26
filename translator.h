#pragma once

#include <string>

namespace translator
{
    struct Result
    {
        std::wstring text;
        std::string detectedLanguage;
    };

    Result translate_auto(const std::wstring& text, const std::string& targetLanguage);
    bool language_matches(const std::string& detected, const std::string& expected);
}
