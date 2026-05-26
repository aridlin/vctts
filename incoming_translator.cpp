#include "incoming_translator.h"

#include "app_audio_control.h"
#include "custom_tts.h"

#include <windows.h>

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

namespace
{
    std::thread g_worker;
    std::atomic<bool> g_stop{false};
    std::mutex g_mutex;
    std::wstring g_lastMutedExe;

    void unmute_last()
    {
        if (!g_lastMutedExe.empty()) {
            app_audio_control::set_process_mute_by_exe(g_lastMutedExe, false);
            g_lastMutedExe.clear();
        }
    }

    void worker(AppState* state)
    {
        while (!g_stop.load())
        {
            if (!state) break;

            const bool enabled = state->incomingTranslatorMode.load();
            const bool shouldMute = enabled && state->muteIncomingApp.load();
            std::wstring exe = custom_tts::Utf8ToWide(state->incomingAppExe);

            {
                std::lock_guard<std::mutex> lock(g_mutex);
                if (!shouldMute || exe.empty()) {
                    unmute_last();
                } else {
                    if (!g_lastMutedExe.empty() && _wcsicmp(g_lastMutedExe.c_str(), exe.c_str()) != 0)
                        unmute_last();

                    app_audio_control::set_process_mute_by_exe(exe, true);
                    g_lastMutedExe = exe;
                }
            }

            Sleep(500);
        }

        std::lock_guard<std::mutex> lock(g_mutex);
        unmute_last();
    }
}

namespace incoming_translator
{
    void start(AppState& state)
    {
        if (g_worker.joinable()) return;
        g_stop.store(false);
        g_worker = std::thread(worker, &state);
    }

    void stop()
    {
        g_stop.store(true);
        if (g_worker.joinable())
            g_worker.join();
    }
}
