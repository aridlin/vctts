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

using namespace winrt;
using namespace Windows::Media::SpeechSynthesis;
using namespace Windows::Storage::Streams;

namespace
{
    std::mutex g_mutex;
    int g_voiceIndex = -1;
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
        std::vector<std::uint8_t> out;
        if (text.empty())
            return out;

        init_apartment(apartment_type::single_threaded);

        SpeechSynthesizer synth;

        {
            std::lock_guard lock(g_mutex);
            auto voices = SpeechSynthesizer::AllVoices();
            const int n = (int)voices.Size();
            if (n > 0)
            {
                int idx = g_voiceIndex;
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

