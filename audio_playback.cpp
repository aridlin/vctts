#include "audio_playback.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <thread>
#include <vector>
#include <algorithm>
#include <cstring>
#include <string>
#include <cstdint>

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// Cancel/replace mechanism: each playback gets a generation number.
// Starting new playback increments gen; old threads stop when they notice a mismatch.
static std::atomic<std::uint64_t> g_playGen{0};
static std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return {};
    int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring out(len, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), out.data(), len);
    return out;
}

namespace audio_playback
{
    struct DecodedPcm
    {
        std::vector<float> pcm;   // interleaved f32
        ma_uint32 channels = 2;
        ma_uint32 sampleRate = 48000;
        ma_uint64 totalFrames = 0;
    };

    struct Player
    {
        const DecodedPcm* audio = nullptr;
        ma_uint64 cursorFrames = 0;
        std::atomic<bool> done{false};
        std::uint64_t myGen = 0;
    };

    static void data_callback(ma_device* pDevice, void* pOutput, const void*, ma_uint32 frameCount)
    {
        Player* p = (Player*)pDevice->pUserData;
        float* out = (float*)pOutput;

        if (!p || !p->audio || p->myGen != g_playGen.load())
        {
            std::memset(out, 0, (size_t)frameCount * sizeof(float) * pDevice->playback.channels);
            if (p) p->done.store(true);
            return;
        }

        const DecodedPcm& a = *p->audio;
        const ma_uint32 ch = a.channels;

        const ma_uint64 remaining = (p->cursorFrames < a.totalFrames) ? (a.totalFrames - p->cursorFrames) : 0;
        const ma_uint32 toCopy = (ma_uint32)std::min<ma_uint64>(remaining, frameCount);

        if (toCopy > 0)
        {
            const float* src = a.pcm.data() + (size_t)(p->cursorFrames * ch);
            std::memcpy(out, src, (size_t)toCopy * ch * sizeof(float));
            p->cursorFrames += toCopy;
        }

        if (toCopy < frameCount)
        {
            std::memset(out + (size_t)toCopy * ch, 0, (size_t)(frameCount - toCopy) * ch * sizeof(float));
            p->done.store(true);
        }

        if (p->cursorFrames >= a.totalFrames)
            p->done.store(true);
    }

    bool refresh_output_devices(AppState& s)
    {
        s.outDevices.clear();
        s.outDevicesUtf8.clear();

        ma_context ctx;
        if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS)
            return false;

        ma_device_info* playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 captureCount = 0;

        ma_result r = ma_context_get_devices(&ctx, &playbackInfos, &playbackCount, &captureInfos, &captureCount);
        if (r != MA_SUCCESS)
        {
            ma_context_uninit(&ctx);
            return false;
        }

        for (ma_uint32 i = 0; i < playbackCount; i++)
        {
            const char* nm = playbackInfos[i].name;
            std::string nameUtf8 = nm ? nm : "(unknown)";

            AudioDevice ad;
            ad.id = L""; // selection is by index, ID not needed in AppState
            ad.name = Utf8ToWide(nameUtf8);

            s.outDevices.push_back(std::move(ad));
            s.outDevicesUtf8.push_back(std::move(nameUtf8));
        }

        ma_context_uninit(&ctx);

        if (s.outDevicesUtf8.empty())
        {
            s.devA = 0;
            s.devB = 0;
            return false;
        }

        if (s.devA < 0 || s.devA >= (int)s.outDevicesUtf8.size()) s.devA = 0;
        if (s.devB < 0 || s.devB >= (int)s.outDevicesUtf8.size()) s.devB = 0;

        return true;
    }

    static bool decode_wav_to_pcm_f32(const std::vector<std::uint8_t>& wav, DecodedPcm& out)
    {
        // Always decode to a stable playback format.
        const ma_uint32 kRate = 48000;
        const ma_uint32 kCh   = 2;

        ma_decoder_config dcfg = ma_decoder_config_init(ma_format_f32, kCh, kRate);

        ma_decoder dec;
        if (ma_decoder_init_memory(wav.data(), wav.size(), &dcfg, &dec) != MA_SUCCESS)
            return false;

        out.channels = kCh;
        out.sampleRate = kRate;

        constexpr ma_uint32 CHUNK_FRAMES = 4096;
        std::vector<float> chunk((size_t)CHUNK_FRAMES * kCh);

        out.pcm.clear();
        out.pcm.reserve((size_t)CHUNK_FRAMES * kCh * 8);

        for (;;)
        {
            ma_uint64 framesRead = 0;
            ma_result rr = ma_decoder_read_pcm_frames(&dec, chunk.data(), CHUNK_FRAMES, &framesRead);

            if (rr != MA_SUCCESS && rr != MA_AT_END)
            {
                ma_decoder_uninit(&dec);
                return false;
            }

            if (framesRead == 0) break;

            const size_t samples = (size_t)framesRead * kCh;
            out.pcm.insert(out.pcm.end(), chunk.begin(), chunk.begin() + samples);

            if (rr == MA_AT_END) break;
        }

        ma_decoder_uninit(&dec);

        out.totalFrames = (ma_uint64)(out.pcm.size() / kCh);
        return out.totalFrames > 0;
    }

    void stop_all()
    {
        g_playGen.fetch_add(1);
    }

    void play_wav_to_selected_async(const std::vector<std::uint8_t>& wav, int devA, int devB)
    {
        if (wav.empty()) return;

        const int idxA = devA;
        const int idxB = devB;

        const std::uint64_t myGen = g_playGen.fetch_add(1) + 1;

        std::vector<std::uint8_t> wavCopy = wav;

        std::thread([wavCopy = std::move(wavCopy), idxA, idxB, myGen]() mutable {
            ma_context ctx;
            if (ma_context_init(nullptr, 0, nullptr, &ctx) != MA_SUCCESS)
                return;

            ma_device_info* playbackInfos = nullptr;
            ma_uint32 playbackCount = 0;
            ma_device_info* captureInfos = nullptr;
            ma_uint32 captureCount = 0;

            if (ma_context_get_devices(&ctx, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS ||
                playbackCount == 0)
            {
                ma_context_uninit(&ctx);
                return;
            }

            int a = idxA;
            int b = idxB;
            if (a < 0 || a >= (int)playbackCount) a = 0;
            if (b < 0 || b >= (int)playbackCount) b = 0;

            // Decode WAV -> PCM float
            DecodedPcm audio;
            if (!decode_wav_to_pcm_f32(wavCopy, audio))
            {
                ma_context_uninit(&ctx);
                return;
            }

            // If both outputs are the same device, just play once.
            const bool single = (a == b);

            ma_device_id devIdA = playbackInfos[a].id;
            ma_device_id devIdB = playbackInfos[b].id;

            Player pA; pA.audio = &audio; pA.cursorFrames = 0; pA.done.store(false); pA.myGen = myGen;
            Player pB; pB.audio = &audio; pB.cursorFrames = 0; pB.done.store(false); pB.myGen = myGen;

            ma_device_config cfgA = ma_device_config_init(ma_device_type_playback);
            cfgA.playback.pDeviceID = &devIdA;
            cfgA.playback.format    = ma_format_f32;
            cfgA.playback.channels  = audio.channels;
            cfgA.sampleRate         = audio.sampleRate;
            cfgA.dataCallback       = data_callback;
            cfgA.pUserData          = &pA;
            cfgA.performanceProfile = ma_performance_profile_low_latency;

            ma_device devAInst{};
            if (ma_device_init(&ctx, &cfgA, &devAInst) != MA_SUCCESS)
            {
                ma_context_uninit(&ctx);
                return;
            }

            ma_device devBInst{};
            bool devBStarted = false;

            if (!single)
            {
                ma_device_config cfgB = ma_device_config_init(ma_device_type_playback);
                cfgB.playback.pDeviceID = &devIdB;
                cfgB.playback.format    = ma_format_f32;
                cfgB.playback.channels  = audio.channels;
                cfgB.sampleRate         = audio.sampleRate;
                cfgB.dataCallback       = data_callback;
                cfgB.pUserData          = &pB;
                cfgB.performanceProfile = ma_performance_profile_low_latency;

                if (ma_device_init(&ctx, &cfgB, &devBInst) == MA_SUCCESS)
                    devBStarted = true;
                else
                    pB.done.store(true); // treat as done if second device fails
            }
            else
            {
                pB.done.store(true);
            }

            // Start A then B immediately after.
            ma_device_start(&devAInst);
            if (devBStarted)
                ma_device_start(&devBInst);

            while (g_playGen.load() == myGen && (!pA.done.load() || !pB.done.load()))
                Sleep(5);

            if (devBStarted)
                ma_device_uninit(&devBInst);

            ma_device_uninit(&devAInst);
            ma_context_uninit(&ctx);
        }).detach();
    }
}

