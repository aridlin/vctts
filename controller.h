#pragma once
#include "app_state.h"
#include "win32_window.h"

#include <string>
#include <chrono>

// Optional callback: later we’ll hook this to SAPI->WAV->playback.
struct ControllerCallbacks
{
    // Called when recording stops (Enter/timeout/button) with the final text.
    void (*onCommittedText)(const std::wstring& text) = nullptr;
};

struct Controller
{
    AppState& s;
    ControllerCallbacks cb{};

    explicit Controller(AppState& state, ControllerCallbacks callbacks = {})
        : s(state), cb(callbacks) {}

    // Start recording: clear buffer, show window, steal focus, remember previous foreground.
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

        // You said focus steal is OK.
        SetForegroundWindow(s.hwnd);
        SetActiveWindow(s.hwnd);
    }

    // Stop recording: hide window, restore focus, emit committed text.
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

        if (s.recording.load()) stop_recording();
        else start_recording();
    }

    // Called each frame (or periodically) to enforce timeout.
    void tick_timeout()
    {
        if (!s.recording.load()) return;

        auto now = std::chrono::steady_clock::now();
        auto dt = std::chrono::duration_cast<std::chrono::seconds>(now - s.lastInput).count();
        if (dt >= s.timeoutSeconds)
            stop_recording();
    }

    void request_exit()
    {
        s.exitRequested.store(true);
    }

    // These helpers are meant to be called by keyboard hook callbacks:
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

