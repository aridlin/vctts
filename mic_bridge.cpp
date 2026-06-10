#include "mic_bridge.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <mutex>
#include <vector>

#include "miniaudio.h"

namespace
{
    struct DecodedPcm
    {
        std::vector<float> pcm;
        ma_uint32 channels = 2;
        ma_uint32 sampleRate = 48000;
        ma_uint64 totalFrames = 0;
    };

    struct BridgeState
    {
        ma_context context{};
        ma_device device{};
        bool contextReady = false;
        bool deviceReady = false;
        int micIndex = -1;
        int outIndex = -1;
        AppState* app = nullptr;
        std::mutex ttsMutex;
        std::vector<float> ttsPcm;
        ma_uint64 ttsCursor = 0;
    };

    BridgeState g_bridge;

    float soft_clip(float x)
    {
        return std::tanh(x);
    }

    bool decode_audio_to_pcm_f32(const std::vector<std::uint8_t>& audioData, DecodedPcm& out)
    {
        if (audioData.empty()) return false;

        const ma_uint32 kRate = 48000;
        const ma_uint32 kCh = 2;
        ma_decoder_config dcfg = ma_decoder_config_init(ma_format_f32, kCh, kRate);

        ma_decoder dec;
        if (ma_decoder_init_memory(audioData.data(), audioData.size(), &dcfg, &dec) != MA_SUCCESS)
            return false;

        out.channels = kCh;
        out.sampleRate = kRate;
        out.pcm.clear();

        constexpr ma_uint32 chunkFrames = 4096;
        std::vector<float> chunk((size_t)chunkFrames * kCh);
        for (;;) {
            ma_uint64 framesRead = 0;
            ma_result rr = ma_decoder_read_pcm_frames(&dec, chunk.data(), chunkFrames, &framesRead);
            if (rr != MA_SUCCESS && rr != MA_AT_END) {
                ma_decoder_uninit(&dec);
                return false;
            }
            if (framesRead == 0) break;
            out.pcm.insert(out.pcm.end(), chunk.begin(), chunk.begin() + (size_t)framesRead * kCh);
            if (rr == MA_AT_END) break;
        }

        ma_decoder_uninit(&dec);
        out.totalFrames = (ma_uint64)(out.pcm.size() / kCh);
        return out.totalFrames > 0;
    }

    void data_callback(ma_device*, void* output, const void* input, ma_uint32 frameCount)
    {
        float* out = (float*)output;
        const float* in = (const float*)input;
        if (!out) return;

        AppState* app = g_bridge.app;
        const bool micMuted = app && app->micMuted.load();
        const float micGain = app ? std::clamp(app->micGain, 0.0f, 3.0f) : 1.0f;
        const float ttsGain = app ? std::clamp(app->ttsGain, 0.0f, 3.0f) : 1.0f;

        std::lock_guard<std::mutex> lock(g_bridge.ttsMutex);
        const ma_uint32 channels = 2;
        for (ma_uint32 frame = 0; frame < frameCount; ++frame) {
            for (ma_uint32 ch = 0; ch < channels; ++ch) {
                const size_t sample = (size_t)frame * channels + ch;
                float mixed = 0.0f;
                if (!micMuted && in)
                    mixed += in[sample] * micGain;

                if (g_bridge.ttsCursor < g_bridge.ttsPcm.size() / channels) {
                    const size_t ttsSample = (size_t)g_bridge.ttsCursor * channels + ch;
                    mixed += g_bridge.ttsPcm[ttsSample] * ttsGain;
                }

                out[sample] = soft_clip(mixed);
            }
            if (g_bridge.ttsCursor < g_bridge.ttsPcm.size() / channels)
                ++g_bridge.ttsCursor;
        }

        if (g_bridge.ttsCursor >= g_bridge.ttsPcm.size() / channels) {
            g_bridge.ttsPcm.clear();
            g_bridge.ttsCursor = 0;
        }
    }

    void stop_locked()
    {
        if (g_bridge.deviceReady) {
            ma_device_uninit(&g_bridge.device);
            g_bridge.deviceReady = false;
        }
        if (g_bridge.contextReady) {
            ma_context_uninit(&g_bridge.context);
            g_bridge.contextReady = false;
        }
        g_bridge.micIndex = -1;
        g_bridge.outIndex = -1;
        g_bridge.app = nullptr;
        std::lock_guard<std::mutex> lock(g_bridge.ttsMutex);
        g_bridge.ttsPcm.clear();
        g_bridge.ttsCursor = 0;
    }

    bool start(AppState& state)
    {
        if (state.inDevicesUtf8.empty() || state.outDevicesUtf8.empty())
            return false;

        if (ma_context_init(nullptr, 0, nullptr, &g_bridge.context) != MA_SUCCESS)
            return false;
        g_bridge.contextReady = true;

        ma_device_info* playbackInfos = nullptr;
        ma_uint32 playbackCount = 0;
        ma_device_info* captureInfos = nullptr;
        ma_uint32 captureCount = 0;
        if (ma_context_get_devices(&g_bridge.context, &playbackInfos, &playbackCount, &captureInfos, &captureCount) != MA_SUCCESS) {
            stop_locked();
            return false;
        }

        int mic = state.micDev;
        int out = state.bridgeVirtualOutDev;
        if (mic < 0 || mic >= (int)captureCount || out < 0 || out >= (int)playbackCount) {
            stop_locked();
            return false;
        }

        ma_device_id micId = captureInfos[mic].id;
        ma_device_id outId = playbackInfos[out].id;

        ma_device_config cfg = ma_device_config_init(ma_device_type_duplex);
        cfg.capture.pDeviceID = &micId;
        cfg.capture.format = ma_format_f32;
        cfg.capture.channels = 2;
        cfg.playback.pDeviceID = &outId;
        cfg.playback.format = ma_format_f32;
        cfg.playback.channels = 2;
        cfg.sampleRate = 48000;
        cfg.dataCallback = data_callback;

        if (ma_device_init(&g_bridge.context, &cfg, &g_bridge.device) != MA_SUCCESS) {
            stop_locked();
            return false;
        }
        g_bridge.deviceReady = true;
        g_bridge.app = &state;
        g_bridge.micIndex = mic;
        g_bridge.outIndex = out;

        if (ma_device_start(&g_bridge.device) != MA_SUCCESS) {
            stop_locked();
            return false;
        }
        return true;
    }
}

namespace mic_bridge
{
    bool sync(AppState& state)
    {
        if (!state.micBridgeEnabled.load()) {
            stop();
            return false;
        }

        if (g_bridge.deviceReady &&
            g_bridge.micIndex == state.micDev &&
            g_bridge.outIndex == state.bridgeVirtualOutDev) {
            return true;
        }

        stop();
        return start(state);
    }

    bool submit_tts(const std::vector<std::uint8_t>& audioBytes, AppState& state)
    {
        if (!sync(state))
            return false;

        DecodedPcm decoded;
        if (!decode_audio_to_pcm_f32(audioBytes, decoded))
            return false;

        std::lock_guard<std::mutex> lock(g_bridge.ttsMutex);
        if (g_bridge.ttsCursor >= g_bridge.ttsPcm.size() / 2) {
            g_bridge.ttsPcm = std::move(decoded.pcm);
            g_bridge.ttsCursor = 0;
        } else {
            size_t start = (size_t)g_bridge.ttsCursor * 2;
            std::vector<float> remaining(g_bridge.ttsPcm.begin() + start, g_bridge.ttsPcm.end());
            remaining.insert(remaining.end(), decoded.pcm.begin(), decoded.pcm.end());
            g_bridge.ttsPcm = std::move(remaining);
            g_bridge.ttsCursor = 0;
        }
        return true;
    }

    void stop()
    {
        stop_locked();
    }

    bool running()
    {
        return g_bridge.deviceReady;
    }
}
