#include "tts_winrt.h"

// Must come BEFORE winrt headers so GUID/_GUID is fully defined.
#include <windows.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.SpeechSynthesis.h>
#include <winrt/Windows.Storage.Streams.h>

#include <vector>
#include <mutex>
#include <algorithm>
#include <string>
#include <cctype>

using namespace winrt;
using namespace Windows::Media::SpeechSynthesis;
using namespace Windows::Storage::Streams;

namespace
{
    std::mutex g_mutex;
    int g_voiceIndex = -1;

    std::string WideToUtf8(const std::wstring& s)
    {
        if (s.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    std::string normalize_language(std::string lang)
    {
        std::transform(lang.begin(), lang.end(), lang.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        return lang;
    }

    bool language_matches(const std::string& voiceLanguage, const std::string& requested)
    {
        std::string a = normalize_language(voiceLanguage);
        std::string b = normalize_language(requested);
        if (a.empty() || b.empty()) return false;
        return a == b || a.rfind(b + "-", 0) == 0 || b.rfind(a + "-", 0) == 0;
    }
}

namespace tts_winrt
{
    std::vector<std::wstring> list_voices()
    {
        init_apartment(apartment_type::single_threaded);

        std::vector<std::wstring> out;

        auto voices = SpeechSynthesizer::AllVoices();
        const uint32_t n = voices.Size();

        out.reserve(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            auto v = voices.GetAt(i);
            out.emplace_back(v.DisplayName().c_str());
        }

        return out;
    }

    void set_voice_index(int index)
    {
        std::lock_guard lock(g_mutex);
        g_voiceIndex = index;
    }

    std::vector<std::uint8_t> speak_wav(const std::wstring& text)
    {
        return speak_wav_with_language(text, {});
    }

    std::vector<std::uint8_t> speak_wav_with_language(
        const std::wstring& text,
        const std::string& language)
    {
        std::vector<std::uint8_t> out;
        if (text.empty())
            return out;

        init_apartment(apartment_type::multi_threaded);

        SpeechSynthesizer synth;

        {
            std::lock_guard lock(g_mutex);
            auto voices = SpeechSynthesizer::AllVoices();
            const int n = (int)voices.Size();
            if (n > 0)
            {
                int idx = g_voiceIndex;
                if (!language.empty())
                {
                    for (int i = 0; i < n; ++i)
                    {
                        std::string voiceLang = WideToUtf8(voices.GetAt((uint32_t)i).Language().c_str());
                        if (language_matches(voiceLang, language))
                        {
                            idx = i;
                            break;
                        }
                    }
                }
                if (idx < 0 || idx >= n) idx = 0;
                synth.Voice(voices.GetAt((uint32_t)idx));
            }
        }

        auto stream = synth.SynthesizeTextToStreamAsync(text).get();
        const uint64_t size64 = stream.Size();
        if (size64 == 0)
            return out;

        const uint32_t size = (size64 > 0xFFFFFFFFull) ? 0xFFFFFFFFu : (uint32_t)size64;

        out.resize((size_t)size);

        DataReader reader(stream);
        reader.LoadAsync(size).get();
        reader.ReadBytes(out);

        return out;
    }
}
