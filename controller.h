#pragma once
#include "app_state.h"
#include "win32_window.h"

#include <chrono>
#include <string>

struct ControllerCallbacks {
    void (*onCommittedText)(const std::wstring& text) = nullptr;
};

struct Controller {
    AppState& s;
    ControllerCallbacks cb{};

    explicit Controller(AppState& state, ControllerCallbacks callbacks = {})
        : s(state), cb(callbacks) {}

    void start_recording()
    {
        if (s.recording.load()) return;
        if (!s.configDone.load()) return;

        win32_window::save_foreground(s);

        s.clearBuffer();
        s.lastInput = std::chrono::steady_clock::now();
        s.recording.store(true);

        win32_window::show(s);
        win32_window::set_topmost(s, true);

        // ✅ THIS is the important change
        win32_window::force_foreground(s.hwnd);
    }

    void stop_recording()
    {
        if (!s.recording.load()) return;

        s.recording.store(false);

        win32_window::hide(s);
        win32_window::restore_foreground(s);

        std::wstring text = s.takeBufferAndClear();
        if (cb.onCommittedText && !text.empty())
            cb.onCommittedText(text);
    }

    void toggle_recording()
    {
        if (!s.configDone.load()) return;

        auto now = std::chrono::steady_clock::now();
        double dt = std::chrono::duration<double>(now - s.lastToggle).count();
        if (dt < s.toggleDebounceSec) return;
        s.lastToggle = now;

        if (s.recording.load())
            stop_recording();
        else
            start_recording();
    }

    void tick_timeout()
    {
        if (!s.recording.load()) return;

        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::seconds>(
            now - s.lastInput
        ).count();

        if (dt >= s.timeoutSeconds)
            stop_recording();
    }

    void on_append_text(const wchar_t* text, int count)
    {
        if (!s.recording.load()) return;
        if (!text || count <= 0) return;
        s.appendSpan(text, (size_t)count);
    }

    void on_backspace()
    {
        if (!s.recording.load()) return;
        s.backspace();
    }
};

