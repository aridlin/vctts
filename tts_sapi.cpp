#include "tts_sapi.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <sapi.h>
#include <wrl/client.h>
#include <objbase.h>

#include <vector>
#include <cstdint>

using Microsoft::WRL::ComPtr;

namespace tts_sapi
{
    static void fill_wfx(WAVEFORMATEX& wfx, int sampleRate, int channels, int bits)
    {
        wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = (WORD)channels;
        wfx.nSamplesPerSec = (DWORD)sampleRate;
        wfx.wBitsPerSample = (WORD)bits;
        wfx.nBlockAlign = (WORD)((wfx.nChannels * wfx.wBitsPerSample) / 8);
        wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;
        wfx.cbSize = 0;
    }

    std::vector<uint8_t> speak_to_wav_memory(
        const std::wstring& text,
        int sampleRate,
        int channels,
        int bits,
        int rate,
        int volume)
    {
        std::vector<uint8_t> out;
        if (text.empty()) return out;

        // Clamp-ish
        if (sampleRate <= 0) sampleRate = 22050;
        if (channels <= 0) channels = 1;
        if (bits != 8 && bits != 16) bits = 16;
        if (volume < 0) volume = 0;
        if (volume > 100) volume = 100;

        // COM init per-thread
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool didInit = SUCCEEDED(hr);
        // If already initialized with different mode, we can still proceed; just don't uninit.
        if (hr == RPC_E_CHANGED_MODE) didInit = false;
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) return out;

        // Create voice
        ComPtr<ISpVoice> voice;
        hr = CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&voice));
        if (FAILED(hr) || !voice) {
            if (didInit) CoUninitialize();
            return out;
        }

        voice->SetRate((long)rate);
        voice->SetVolume((USHORT)volume);

        // Create an IStream backed by HGLOBAL
        ComPtr<IStream> baseStream;
        hr = CreateStreamOnHGlobal(NULL, TRUE, &baseStream);
        if (FAILED(hr) || !baseStream) {
            if (didInit) CoUninitialize();
            return out;
        }

        // Create SAPI stream and bind to baseStream as WAV
        ComPtr<ISpStream> spStream;
        hr = CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL, IID_PPV_ARGS(&spStream));
        if (FAILED(hr) || !spStream) {
            if (didInit) CoUninitialize();
            return out;
        }

        WAVEFORMATEX wfx{};
        fill_wfx(wfx, sampleRate, channels, bits);

        hr = spStream->SetBaseStream(baseStream.Get(), SPDFID_WaveFormatEx, &wfx);
        if (FAILED(hr)) {
            if (didInit) CoUninitialize();
            return out;
        }

        // Direct output to our stream
        hr = voice->SetOutput(spStream.Get(), TRUE);
        if (FAILED(hr)) {
            if (didInit) CoUninitialize();
            return out;
        }

        // Speak synchronously into the stream
        hr = voice->Speak(text.c_str(), SPF_DEFAULT, nullptr);

        // Restore output back to default (best effort)
        voice->SetOutput(nullptr, TRUE);

        if (FAILED(hr)) {
            if (didInit) CoUninitialize();
            return out;
        }

        // Extract HGLOBAL from IStream and copy bytes
        HGLOBAL hGlobal = NULL;
        hr = GetHGlobalFromStream(baseStream.Get(), &hGlobal);
        if (FAILED(hr) || !hGlobal) {
            if (didInit) CoUninitialize();
            return out;
        }

        SIZE_T sz = GlobalSize(hGlobal);
        if (sz == 0) {
            if (didInit) CoUninitialize();
            return out;
        }

        void* ptr = GlobalLock(hGlobal);
        if (!ptr) {
            if (didInit) CoUninitialize();
            return out;
        }

        out.resize(sz);
        memcpy(out.data(), ptr, sz);

        GlobalUnlock(hGlobal);

        if (didInit) CoUninitialize();
        return out;
    }
}

