#include "tts_sapi.h"

#include <windows.h>
#include <sapi.h>
#include <sphelper.h>
#include <wrl/client.h>
#include <objbase.h>
#include <mmreg.h>

#include <vector>
#include <cstdint>
#include <cstring>

using Microsoft::WRL::ComPtr;

#define TTS_DEBUG(x) OutputDebugStringW(L"[TTS] " x L"\n")

namespace {
    int g_voiceIndex = 0;
}

namespace tts_sapi
{
    static void fill_wfx_16k_mono_16(WAVEFORMATEX& wfx)
    {
        wfx = {};
        wfx.wFormatTag = WAVE_FORMAT_PCM;
        wfx.nChannels = 1;
        wfx.nSamplesPerSec = 16000;
        wfx.wBitsPerSample = 16;
        wfx.nBlockAlign = 2;
        wfx.nAvgBytesPerSec = 32000;
        wfx.cbSize = 0;
    }

    std::vector<std::wstring> list_voices()
    {
        std::vector<std::wstring> out;

        ComPtr<IEnumSpObjectTokens> e;
        if (FAILED(SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &e)))
            return out;

        ULONG count = 0;
        e->GetCount(&count);

        for (ULONG i = 0; i < count; ++i)
        {
            ComPtr<ISpObjectToken> tok;
            if (FAILED(e->Item(i, &tok))) continue;

            CSpDynamicString desc;
            if (SUCCEEDED(SpGetDescription(tok.Get(), &desc)) && desc.m_psz)
                out.emplace_back(desc.m_psz);
        }

        return out;
    }

    void set_voice_index(int index)
    {
        g_voiceIndex = index;
    }

    std::vector<std::uint8_t> speak_to_wav_memory(
        const std::wstring& text,
        int rate,
        int volume)
    {
        std::vector<std::uint8_t> out;
        if (text.empty()) return out;

        HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const bool didInit = SUCCEEDED(hr);
        if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
            return out;

        ComPtr<ISpVoice> voice;
        if (FAILED(CoCreateInstance(CLSID_SpVoice, nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&voice))))
            goto cleanup;

        // Select voice by index
        {
            ComPtr<IEnumSpObjectTokens> e;
            if (SUCCEEDED(SpEnumTokens(SPCAT_VOICES, nullptr, nullptr, &e)))
            {
                ULONG count = 0;
                e->GetCount(&count);
                if (count > 0)
                {
                    int idx = g_voiceIndex;
                    if (idx < 0) idx = 0;
                    if (idx >= (int)count) idx = (int)count - 1;

                    ComPtr<ISpObjectToken> tok;
                    if (SUCCEEDED(e->Item((ULONG)idx, &tok)))
                        voice->SetVoice(tok.Get());
                }
            }
        }

        voice->SetRate(rate);
        voice->SetVolume((USHORT)volume);

        ComPtr<IStream> mem;
        if (FAILED(CreateStreamOnHGlobal(NULL, TRUE, &mem)))
            goto cleanup;

        ComPtr<ISpStream> stream;
        if (FAILED(CoCreateInstance(CLSID_SpStream, nullptr, CLSCTX_ALL,
                                    IID_PPV_ARGS(&stream))))
            goto cleanup;

        WAVEFORMATEX wfx;
        fill_wfx_16k_mono_16(wfx);

        if (FAILED(stream->SetBaseStream(mem.Get(), SPDFID_WaveFormatEx, &wfx)))
            goto cleanup;

        if (FAILED(voice->SetOutput(stream.Get(), TRUE)))
            goto cleanup;

        TTS_DEBUG(L"SAPI Speak()");
        voice->Speak(text.c_str(), SPF_DEFAULT, nullptr);
        voice->WaitUntilDone(INFINITE);

        voice->SetOutput(nullptr, TRUE);
        stream->Close();
        mem->Commit(STGC_DEFAULT);

        HGLOBAL hg = nullptr;
        if (FAILED(GetHGlobalFromStream(mem.Get(), &hg)) || !hg)
            goto cleanup;

        SIZE_T sz = GlobalSize(hg);
        if (sz < 44)
            goto cleanup;

        if (void* p = GlobalLock(hg))
        {
            out.resize(sz);
            std::memcpy(out.data(), p, sz);
            GlobalUnlock(hg);
        }

    cleanup:
        if (didInit)
            CoUninitialize();
        return out;
    }
}

