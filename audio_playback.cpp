#include "audio_playback.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <objbase.h>   // CoInitializeEx

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

// IMPORTANT: exactly ONE translation unit must define MINIAUDIO_IMPLEMENTATION.
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

static std::atomic<std::uint64_t> g_playGen{0};

static std::mutex g_cacheMutex;
static std::vector<ma_device_id> g_cachedIds;
static std::vector<std::string>  g_cachedNames;

static void dbg(const char* s)
{
    OutputDebugStringA(s);
    OutputDebugStringA("\n");
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

    static void data_callback(ma_device* dev, void* out, const void*, ma_uint32 frameCount)
    {
        Player* p = (Player*)dev->pUserData;
        float*  f = (float*)out;
        const ma_uint32 ch = dev->playback.channels;

        // Cancel => silence + done.
        if (!p || !p->audio || p->myGen != g_playGen.load())
        {
            std::memset(f, 0, (size_t)frameCount * ch * sizeof(float));
            if (p) p->done.store(true);
            return;
        }

        const DecodedPcm& a = *p->audio;
        const ma_uint64 remaining = (p->cursorFrames < a.totalFrames) ? (a.totalFrames - p->cursorFrames) : 0;
        const ma_uint32 toCopy = (ma_uint32)std::min<ma_uint64>(remaining, frameCount);

        if (toCopy > 0)
        {
            const float* src = a.pcm.data() + (size_t)(p->cursorFrames * a.channels);
            std::memcpy(f, src, (size_t)toCopy * a.channels * sizeof(float));
            p->cursorFrames += toCopy;
        }

        if (toCopy < frameCount)
        {
            std::memset(f + (size_t)toCopy * a.channels, 0, (size_t)(frameCount - toCopy) * a.channels * sizeof(float));
            p->done.store(true);
        }

        if (p->cursorFrames >= a.totalFrames)
            p->done.store(true);
    }

    static bool decode_wav_to_pcm_f32_stereo48k(const std::vector<std::uint8_t>& wav, DecodedPcm& out)
    {
        const ma_uint32 kRate = 48000;
        const ma_uint32 kCh   = 2;

        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, kCh, kRate);

        ma_decoder dec{};
        if (ma_decoder_init_memory(wav.data(), wav.size(), &cfg, &dec) != MA_SUCCESS)
        {
            dbg("[audio] ma_decoder_init_memory failed");
            return false;
        }

        out.channels   = kCh;
        out.sampleRate = kRate;
        out.pcm.clear();

        constexpr ma_uint32 CHUNK_FRAMES = 4096;
        std::vector<float> chunk((size_t)CHUNK_FRAMES * kCh);

        for (;;)
        {
            ma_uint64 framesRead = 0;
            ma_result rr = ma_decoder_read_pcm_frames(&dec, chunk.data(), CHUNK_FRAMES, &framesRead);

            if (rr != MA_SUCCESS && rr != MA_AT_END)
            {
                dbg("[audio] ma_decoder_read_pcm_frames failed");
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

    bool refresh_output_devices(AppState& s)
    {
        s.outDevices.clear();
        s.outDevicesUtf8.clear();

        // Ensure COM on the calling thread (safe if already inited).
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        bool didCom = SUCCEEDED(hr);
        if (hr == RPC_E_CHANGED_MODE) didCom = false;

        ma_backend backends[] = { ma_backend_wasapi };
        ma_context ctx{};
        if (ma_context_init(backends, 1, nullptr, &ctx) != MA_SUCCESS)
        {
            dbg("[audio] ma_context_init failed");
            if (didCom) CoUninitialize();
            return false;
        }

        ma_device_info* playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 captureCount = 0;

        if (ma_context_get_devices(&ctx, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS)
        {
            dbg("[audio] ma_context_get_devices failed");
            ma_context_uninit(&ctx);
            if (didCom) CoUninitialize();
            return false;
        }

        {
            std::lock_guard<std::mutex> lk(g_cacheMutex);
            g_cachedIds.clear();
            g_cachedNames.clear();
            g_cachedIds.reserve(playbackCount);
            g_cachedNames.reserve(playbackCount);

            for (ma_uint32 i = 0; i < playbackCount; i++)
            {
                g_cachedIds.push_back(playbackInfos[i].id);
                const char* nm = playbackInfos[i].name;
                g_cachedNames.push_back((nm && nm[0]) ? nm : "(unknown)");
            }
        }

        for (ma_uint32 i = 0; i < playbackCount; i++)
        {
            AudioDevice ad;
            ad.id = L""; // index-based for now; miniaudio id cached internally
            // We only need a display name in UI; keep utf8 in outDevicesUtf8.
            ad.name = L""; // optional
            s.outDevices.push_back(ad);

            const char* nm = playbackInfos[i].name;
            s.outDevicesUtf8.push_back((nm && nm[0]) ? nm : "(unknown)");
        }

        ma_context_uninit(&ctx);
        if (didCom) CoUninitialize();

        if (s.outDevicesUtf8.empty())
        {
            s.devA = 0; s.devB = 0;
            return false;
        }

        if (s.devA < 0 || s.devA >= (int)s.outDevicesUtf8.size()) s.devA = 0;
        if (s.devB < 0 || s.devB >= (int)s.outDevicesUtf8.size()) s.devB = 0;
        return true;
    }

    void stop_all()
    {
        g_playGen.fetch_add(1);
    }

    std::vector<std::uint8_t> make_test_tone_wav(int hz, int ms, int sampleRate)
    {
        if (hz <= 0) hz = 440;
        if (ms <= 0) ms = 200;
        if (sampleRate <= 0) sampleRate = 48000;

        const int channels = 1;
        const int bits = 16;
        const int bytesPerSample = bits / 8;
        const int totalSamples = (sampleRate * ms) / 1000;
        const int dataBytes = totalSamples * channels * bytesPerSample;

        auto write_u32 = [](std::vector<std::uint8_t>& v, std::uint32_t x)
        {
            v.push_back((std::uint8_t)(x & 0xFF));
            v.push_back((std::uint8_t)((x >> 8) & 0xFF));
            v.push_back((std::uint8_t)((x >> 16) & 0xFF));
            v.push_back((std::uint8_t)((x >> 24) & 0xFF));
        };
        auto write_u16 = [](std::vector<std::uint8_t>& v, std::uint16_t x)
        {
            v.push_back((std::uint8_t)(x & 0xFF));
            v.push_back((std::uint8_t)((x >> 8) & 0xFF));
        };
        auto write_tag = [](std::vector<std::uint8_t>& v, const char* t)
        {
            v.push_back((std::uint8_t)t[0]);
            v.push_back((std::uint8_t)t[1]);
            v.push_back((std::uint8_t)t[2]);
            v.push_back((std::uint8_t)t[3]);
        };

        std::vector<std::uint8_t> wav;
        wav.reserve(44 + dataBytes);

        // RIFF header
        write_tag(wav, "RIFF");
        write_u32(wav, 36u + (std::uint32_t)dataBytes);
        write_tag(wav, "WAVE");

        // fmt chunk
        write_tag(wav, "fmt ");
        write_u32(wav, 16);
        write_u16(wav, 1); // PCM
        write_u16(wav, (std::uint16_t)channels);
        write_u32(wav, (std::uint32_t)sampleRate);
        write_u32(wav, (std::uint32_t)(sampleRate * channels * bytesPerSample));
        write_u16(wav, (std::uint16_t)(channels * bytesPerSample));
        write_u16(wav, (std::uint16_t)bits);

        // data chunk
        write_tag(wav, "data");
        write_u32(wav, (std::uint32_t)dataBytes);

        // samples
        const double twoPi = 6.2831853071795864769;
        const double amp = 0.25; // keep it gentle
        for (int i = 0; i < totalSamples; i++)
        {
            double t = (double)i / (double)sampleRate;
            double s = std::sin(twoPi * (double)hz * t) * amp;
            int sample = (int)std::lround(s * 32767.0);
            if (sample < -32768) sample = -32768;
            if (sample >  32767) sample =  32767;

            std::int16_t v = (std::int16_t)sample;
            wav.push_back((std::uint8_t)(v & 0xFF));
            wav.push_back((std::uint8_t)((v >> 8) & 0xFF));
        }

        return wav;
    }

    void play_wav_to_selected_async(const std::vector<std::uint8_t>& wav, const AppState& s)
    {
        if (wav.empty()) return;

        // snapshot indices now
        int idxA = s.devA;
        int idxB = s.devB;

        // cancel previous + new generation
        const std::uint64_t myGen = g_playGen.fetch_add(1) + 1;

        // copy wav bytes
        std::vector<std::uint8_t> wavCopy = wav;

        // snapshot cached device IDs
        ma_device_id idA{};
        ma_device_id idB{};
        bool haveA = false, haveB = false;

        {
            std::lock_guard<std::mutex> lk(g_cacheMutex);
            if (!g_cachedIds.empty())
            {
                if (idxA < 0 || idxA >= (int)g_cachedIds.size()) idxA = 0;
                if (idxB < 0 || idxB >= (int)g_cachedIds.size()) idxB = 0;

                idA = g_cachedIds[(size_t)idxA]; haveA = true;
                idB = g_cachedIds[(size_t)idxB]; haveB = true;
            }
        }

        std::thread([wavCopy = std::move(wavCopy), idA, idB, haveA, haveB, idxA, idxB, myGen]() mutable {
            // COM init on THIS thread (critical for WASAPI)
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            bool didCom = SUCCEEDED(hr);
            if (hr == RPC_E_CHANGED_MODE) didCom = false;

            ma_backend backends[] = { ma_backend_wasapi };
            ma_context ctx{};
            if (ma_context_init(backends, 1, nullptr, &ctx) != MA_SUCCESS)
            {
                dbg("[audio] ma_context_init failed in playback thread");
                if (didCom) CoUninitialize();
                return;
            }

            DecodedPcm audio;
            if (!decode_wav_to_pcm_f32_stereo48k(wavCopy, audio))
            {
                dbg("[audio] decode failed, falling back to PlaySoundA (default device)");
                ma_context_uninit(&ctx);

                // fallback: at least prove there is audio
                PlaySoundA((LPCSTR)wavCopy.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);

                if (didCom) CoUninitialize();
                return;
            }

            auto init_one = [&](ma_device& dev, const ma_device_id* pid, Player& player) -> bool {
                ma_device_config cfg = ma_device_config_init(ma_device_type_playback);
                cfg.playback.pDeviceID = pid;               // nullptr = default device
                cfg.playback.format    = ma_format_f32;
                cfg.playback.channels  = audio.channels;
                cfg.sampleRate         = audio.sampleRate;
                cfg.dataCallback       = data_callback;
                cfg.pUserData          = &player;

                ma_result r = ma_device_init(&ctx, &cfg, &dev);
                if (r != MA_SUCCESS)
                    return false;

                if (ma_device_start(&dev) != MA_SUCCESS)
                {
                    ma_device_uninit(&dev);
                    return false;
                }
                return true;
            };

            bool same = haveA && haveB && (std::memcmp(&idA, &idB, sizeof(ma_device_id)) == 0);

            Player pA; pA.audio = &audio; pA.cursorFrames = 0; pA.done.store(false); pA.myGen = myGen;
            Player pB; pB.audio = &audio; pB.cursorFrames = 0; pB.done.store(false); pB.myGen = myGen;

            ma_device devA{};
            ma_device devB{};

            bool okA = false;
            bool okB = false;

            // Try A
            okA = init_one(devA, haveA ? &idA : nullptr, pA);
            if (!okA)
            {
                dbg("[audio] device A init failed, trying default");
                okA = init_one(devA, nullptr, pA);
            }

            // Try B (skip if same device as A)
            if (!same)
            {
                okB = init_one(devB, haveB ? &idB : nullptr, pB);
                if (!okB)
                {
                    dbg("[audio] device B init failed, trying default");
                    okB = init_one(devB, nullptr, pB);
                }
            }
            else
            {
                okB = false;
                pB.done.store(true);
            }

            if (!okA && !okB)
            {
                dbg("[audio] BOTH devices failed, fallback to PlaySoundA");
                ma_context_uninit(&ctx);
                PlaySoundA((LPCSTR)wavCopy.data(), NULL, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
                if (didCom) CoUninitialize();
                return;
            }

            // Wait until done or canceled
            while (g_playGen.load() == myGen && (!pA.done.load() || !pB.done.load()))
                Sleep(5);

            if (okB) ma_device_uninit(&devB);
            if (okA) ma_device_uninit(&devA);

            ma_context_uninit(&ctx);
            if (didCom) CoUninitialize();
        }).detach();
    }
}

