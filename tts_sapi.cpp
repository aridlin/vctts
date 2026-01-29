#include "tts_sapi.h"

#include <windows.h>
#include <sapi.h>
#include <wrl/client.h>
#include <objbase.h>
#include <mmreg.h>

#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace tts_sapi
{
    static void fill_wfx_48k_stereo_16(WAVEFORMATEX& wfx)
    {
        wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 2;
        wfx.nSamplesPerSec = 48000;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;
    }

    static void fill_wfx_16k_mono_16(WAVEFORMATEX& wfx)
    {
        wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = 16000;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = (wfx.nChannels * wfx.wBitsPerSample) / 8;
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;
    }

    std::vector<std::uint8_t> speak_to_wav_memory(const std::wstring& text, int rate, int volume)
    {
        std::vector<std::uint8_t> out;
        if (text.empty()) return out;

        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        bool didInit = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) didInit = false;
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return out;

        ComPtr<ISpVoice> voice;
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice));
        if (FAILED(hr) || !voice) { if (didInit) CoUninitialize(); return out; }

        voice->SetRate((long)rate);
        voice->SetVolume((USHORT)volume);

        ComPtr<IStream> baseStream;
        hr = CreateStreamOnHGlobal(NULL, TRUE, &baseStream);
        if (FAILED(hr) || !baseStream) { if (didInit) CoUninitialize(); return out; }

        ComPtr<ISpStream> spStream;
        hr = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&spStream));
        if (FAILED(hr) || !spStream) { if (didInit) CoUninitialize(); return out; }

        WAVEFORMATEX wfx{};
        fill_wfx_48k_stereo_16(wfx);

        hr = spStream->SetBaseStream(baseStream.Get(), SPDFID_WaveFormatEx, &wfx);
        if (FAILED(hr))
        {
            fill_wfx_16k_mono_16(wfx);
            hr = spStream->SetBaseStream(baseStream.Get(), SPDFID_WaveFormatEx, &wfx);
        }
        if (FAILED(hr)) { if (didInit) CoUninitialize(); return out; }

        hr = voice->SetOutput(spStream.Get(), TRUE);
        if (FAILED(hr)) { if (didInit) CoUninitialize(); return out; }

        hr = voice->Speak(text.c_str(), SPF_DEFAULT, nullptr);

        // IMPORTANT: finalize WAV headers/chunks for strict parsers
        voice->SetOutput(nullptr, TRUE);
        spStream->Close();
        baseStream->Commit(STGC_DEFAULT);

        if (FAILED(hr)) { if (didInit) CoUninitialize(); return out; }

        HGLOBAL hGlobal = NULL;
        hr = GetHGlobalFromStream(baseStream.Get(), &hGlobal);
        if (FAILED(hr) || !hGlobal) { if (didInit) CoUninitialize(); return out; }

        SIZE_T sz = GlobalSize(hGlobal);
        if (sz < 44) { if (didInit) CoUninitialize(); return out; } // must at least have WAV header

        void* ptr = GlobalLock(hGlobal);
        if (!ptr) { if (didInit) CoUninitialize(); return out; }

        out.resize(sz);
        memcpy(out.data(), ptr, sz);
        GlobalUnlock(hGlobal);

        if (didInit) CoUninitialize();
        return out;
    }
}
