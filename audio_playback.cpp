// audio_playback.cpp
#include "audio_playback.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

// exactly ONE translation unit must define MINIAUDIO_IMPLEMENTATION
#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

// ------------------------------------------------------------
// Debug logging (GUI app => use DebugView to see OutputDebugString)
// ------------------------------------------------------------
static void dbg(const char* fmt, ...)
{
    char buf[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    OutputDebugStringA(buf);
    OutputDebugStringA("\n");
}

static std::wstring utf8_to_wide(const std::string& s)
{
    if (s.empty()) return {};
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring out(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), wlen);
    return out;
}

// ------------------------------------------------------------
// Global miniaudio context + cached playback device IDs
// ------------------------------------------------------------
static std::mutex g_maMutex;
static bool g_ctxReady = false;
static ma_context g_ctx;

static std::vector<ma_device_id> g_cachedPlaybackIds;
static std::vector<std::string>  g_cachedPlaybackNames;

static std::atomic<std::uint64_t> g_playGen{0}; // cancel/replace generations

static bool ensure_context()
{
    std::lock_guard<std::mutex> lk(g_maMutex);
    if (g_ctxReady) return true;

    ma_result r = ma_context_init(nullptr, 0, nullptr, &g_ctx);
    if (r != MA_SUCCESS) {
        dbg("[ma] ma_context_init failed: %d", (int)r);
        return false;
    }
    g_ctxReady = true;
    dbg("[ma] context initialized");
    return true;
}

static void refresh_cached_devices_locked()
{
    g_cachedPlaybackIds.clear();
    g_cachedPlaybackNames.clear();

    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    ma_device_info* captureInfos = nullptr;
    ma_uint32 captureCount = 0;

    ma_result r = ma_context_get_devices(&g_ctx, &playbackInfos, &playbackCount, &captureInfos, &captureCount);
    if (r != MA_SUCCESS) {
        dbg("[ma] ma_context_get_devices failed: %d", (int)r);
        return;
    }

    g_cachedPlaybackIds.reserve(playbackCount);
    g_cachedPlaybackNames.reserve(playbackCount);

    for (ma_uint32 i = 0; i < playbackCount; i++) {
        g_cachedPlaybackIds.push_back(playbackInfos[i].id);
        g_cachedPlaybackNames.push_back(playbackInfos[i].name ? playbackInfos[i].name : "(unknown)");
    }

    dbg("[ma] cached %u playback devices", (unsigned)playbackCount);
}

namespace audio_playback
{
    struct DecodedPcm
    {
        std::vector<float> pcm; // interleaved f32
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

    static void data_callback(ma_device* dev, void* outp, const void*, ma_uint32 frameCount)
    {
        Player* p = (Player*)dev->pUserData;
        float* out = (float*)outp;
        const ma_uint32 ch = dev->playback.channels;

        if (!p || !p->audio || p->myGen != g_playGen.load()) {
            std::memset(out, 0, (size_t)frameCount * ch * sizeof(float));
            if (p) p->done.store(true);
            return;
        }

        const DecodedPcm& a = *p->audio;
        const ma_uint64 remaining = (p->cursorFrames < a.totalFrames) ? (a.totalFrames - p->cursorFrames) : 0;
        const ma_uint32 toCopy = (ma_uint32)std::min<ma_uint64>(remaining, frameCount);

        if (toCopy > 0) {
            const float* src = a.pcm.data() + (size_t)(p->cursorFrames * a.channels);
            std::memcpy(out, src, (size_t)toCopy * a.channels * sizeof(float));
            p->cursorFrames += toCopy;
        }

        if (toCopy < frameCount) {
            std::memset(out + (size_t)toCopy * ch, 0, (size_t)(frameCount - toCopy) * ch * sizeof(float));
            p->done.store(true);
        }

        if (p->cursorFrames >= a.totalFrames)
            p->done.store(true);
    }

    static bool decode_wav_to_pcm_f32(const std::vector<std::uint8_t>& wav, DecodedPcm& out)
    {
        const ma_uint32 kRate = 48000;
        const ma_uint32 kCh   = 2;

        ma_decoder_config dcfg = ma_decoder_config_init(ma_format_f32, kCh, kRate);
        ma_decoder dec;

        ma_result r = ma_decoder_init_memory(wav.data(), wav.size(), &dcfg, &dec);
        if (r != MA_SUCCESS) {
            dbg("[ma] ma_decoder_init_memory failed: %d (wav bytes=%zu)", (int)r, wav.size());
            return false;
        }

        out.channels = kCh;
        out.sampleRate = kRate;

        constexpr ma_uint32 CHUNK_FRAMES = 4096;
        std::vector<float> chunk((size_t)CHUNK_FRAMES * kCh);

        out.pcm.clear();
        out.pcm.reserve(chunk.size() * 8);

        for (;;) {
            ma_uint64 framesRead = 0;
            ma_result rr = ma_decoder_read_pcm_frames(&dec, chunk.data(), CHUNK_FRAMES, &framesRead);
            if (rr != MA_SUCCESS && rr != MA_AT_END) {
                dbg("[ma] ma_decoder_read_pcm_frames failed: %d", (int)rr);
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
        dbg("[ma] decoded frames=%llu sr=%u ch=%u", (unsigned long long)out.totalFrames, out.sampleRate, out.channels);
        return out.totalFrames > 0;
    }

    bool refresh_output_devices(AppState& s)
    {
        if (!ensure_context()) return false;

        std::lock_guard<std::mutex> lk(g_maMutex);
        refresh_cached_devices_locked();

        s.outDevices.clear();
        s.outDevicesUtf8.clear();

        for (size_t i = 0; i < g_cachedPlaybackNames.size(); i++) {
            const std::string& nameUtf8 = g_cachedPlaybackNames[i];

            AudioDevice ad;
            ad.id = L""; // not used (we use cached IDs internally)
            ad.name = utf8_to_wide(nameUtf8);

            s.outDevices.push_back(std::move(ad));
            s.outDevicesUtf8.push_back(nameUtf8);
        }

        if (s.outDevicesUtf8.empty()) {
            s.devA = 0; s.devB = 0;
            dbg("[ma] no playback devices");
            return false;
        }

        if (s.devA < 0 || s.devA >= (int)s.outDevicesUtf8.size()) s.devA = 0;
        if (s.devB < 0 || s.devB >= (int)s.outDevicesUtf8.size()) s.devB = 0;

        dbg("[ma] refresh_output_devices ok (count=%zu)", s.outDevicesUtf8.size());
        return true;
    }

    void stop_all()
    {
        g_playGen.fetch_add(1);
        dbg("[ma] stop_all => gen now %llu", (unsigned long long)g_playGen.load());
    }

    void play_wav_to_selected_async(const std::vector<std::uint8_t>& wav, const AppState& s)
    {
        if (wav.empty()) {
            dbg("[ma] play_wav_to_selected_async called with empty wav");
            return;
        }
        if (!ensure_context()) return;

        // Snapshot indices (do NOT touch AppState from worker)
        int idxA = s.devA;
        int idxB = s.devB;

        // Cancel any existing playback and start a new generation.
        const std::uint64_t myGen = g_playGen.fetch_add(1) + 1;

        // Copy WAV bytes (caller may free immediately)
        std::vector<std::uint8_t> wavCopy = wav;

        std::thread([wavCopy = std::move(wavCopy), idxA, idxB, myGen]() mutable {
            // Decode once, share buffer
            DecodedPcm audio;
            if (!decode_wav_to_pcm_f32(wavCopy, audio)) {
                dbg("[ma] decode failed, no playback");
                return;
            }

            // Get device IDs from cached list (same context/backend as the UI list)
            ma_device_id devIdA{};
            ma_device_id devIdB{};
            int count = 0;

            {
                std::lock_guard<std::mutex> lk(g_maMutex);
                count = (int)g_cachedPlaybackIds.size();
                if (count <= 0) {
                    dbg("[ma] cached device list empty, cannot play");
                    return;
                }
                if (idxA < 0 || idxA >= count) idxA = 0;
                if (idxB < 0 || idxB >= count) idxB = 0;

                devIdA = g_cachedPlaybackIds[(size_t)idxA];
                devIdB = g_cachedPlaybackIds[(size_t)idxB];
            }

            // If same device selected for A and B, just play once.
            const bool sameDevice = (idxA == idxB);

            Player pA; pA.audio = &audio; pA.cursorFrames = 0; pA.done.store(false); pA.myGen = myGen;
            Player pB; pB.audio = &audio; pB.cursorFrames = 0; pB.done.store(false); pB.myGen = myGen;

            ma_device devA{};
            ma_device devB{};

            ma_device_config cfgA = ma_device_config_init(ma_device_type_playback);
            cfgA.playback.pDeviceID = &devIdA;
            cfgA.playback.format    = ma_format_f32;
            cfgA.playback.channels  = audio.channels;
            cfgA.sampleRate         = audio.sampleRate;
            cfgA.dataCallback       = data_callback;
            cfgA.pUserData          = &pA;

            ma_result r = ma_device_init(&g_ctx, &cfgA, &devA);
            if (r != MA_SUCCESS) {
                dbg("[ma] ma_device_init(A) failed: %d (idxA=%d)", (int)r, idxA);
                return;
            }

            if (!sameDevice) {
                ma_device_config cfgB = ma_device_config_init(ma_device_type_playback);
                cfgB.playback.pDeviceID = &devIdB;
                cfgB.playback.format    = ma_format_f32;
                cfgB.playback.channels  = audio.channels;
                cfgB.sampleRate         = audio.sampleRate;
                cfgB.dataCallback       = data_callback;
                cfgB.pUserData          = &pB;

                r = ma_device_init(&g_ctx, &cfgB, &devB);
                if (r != MA_SUCCESS) {
                    dbg("[ma] ma_device_init(B) failed: %d (idxB=%d) => playing only A", (int)r, idxB);
                    // fallback: still play A
                    sameDevice ? (void)0 : (void)0;
                    // We'll just not start B.
                    // (No need to return.)
                }
            }

            r = ma_device_start(&devA);
            if (r != MA_SUCCESS) {
                dbg("[ma] ma_device_start(A) failed: %d", (int)r);
                ma_device_uninit(&devA);
                if (!sameDevice) ma_device_uninit(&devB);
                return;
            }

            if (!sameDevice && devB.pContext != nullptr) {
                r = ma_device_start(&devB);
                if (r != MA_SUCCESS) {
                    dbg("[ma] ma_device_start(B) failed: %d (continuing with A)", (int)r);
                    // continue with A only
                }
            }

            dbg("[ma] playback started (A=%d, B=%d, same=%d, gen=%llu)", idxA, idxB, (int)sameDevice, (unsigned long long)myGen);

            // Wait until done OR canceled
            for (;;) {
                if (g_playGen.load() != myGen) break;
                const bool doneA = pA.done.load();
                const bool doneB = sameDevice ? true : pB.done.load();
                if (doneA && doneB) break;
                Sleep(5);
            }

            ma_device_uninit(&devA);
            if (!sameDevice && devB.pContext != nullptr) ma_device_uninit(&devB);

            dbg("[ma] playback finished/canceled (gen=%llu)", (unsigned long long)myGen);
        }).detach();
    }
}

