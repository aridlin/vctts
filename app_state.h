#pragma once
#include <windows.h>

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <vector>

struct AudioDevice {
    std::wstring id;
    std::wstring name;
};

struct AppState {
    // ---- Window / focus ----
    HWND hwnd = nullptr;
    HWND prevForeground = nullptr;

    // ---- Flags ----
    std::atomic<bool> exitRequested{false};
    std::atomic<bool> recording{false};
    std::atomic<bool> configDone{false};
    std::atomic<bool> useKeylessBackup{false};

    // ---- Timings ----
    int timeoutSeconds = 30;
    double toggleDebounceSec = 0.35;
    std::chrono::steady_clock::time_point lastInput = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastToggle = std::chrono::steady_clock::now();

    // ---- Modifiers ----
    std::atomic<bool> shift{false};
    std::atomic<bool> ctrl{false};
    std::atomic<bool> alt{false};

    // ---- Text buffer ----
    mutable std::mutex bufMutex;
    std::wstring buffer;

    // ---- Audio devices ----
    std::vector<AudioDevice> outDevices;
    std::vector<std::string> outDevicesUtf8;
    int devA = 0;
    int devB = 0;

    // ---- SAPI voices ----
    std::vector<std::wstring> sapiVoices;
    int sapiVoiceIndex = 0;

    void clearBuffer() {
        std::lock_guard<std::mutex> lock(bufMutex);
        buffer.clear();
    }

    void appendSpan(const wchar_t* p, size_t n) {
        if (!p || n == 0) return;
        std::lock_guard<std::mutex> lock(bufMutex);
        buffer.append(p, p + n);
        lastInput = std::chrono::steady_clock::now();
    }

    void backspace() {
        std::lock_guard<std::mutex> lock(bufMutex);
        if (!buffer.empty()) buffer.pop_back();
        lastInput = std::chrono::steady_clock::now();
    }

    std::wstring copyBuffer() const {
        std::lock_guard<std::mutex> lock(bufMutex);
        return buffer;
    }

    std::wstring takeBufferAndClear() {
        std::lock_guard<std::mutex> lock(bufMutex);
        std::wstring out = buffer;
        buffer.clear();
        return out;
    }

    static std::string sanitizePreview(const std::wstring& ws) {
        std::string out;
        for (wchar_t c : ws) {
            if (c >= 32 && c < 127) out.push_back((char)c);
            else out.push_back('?');
        }
        return out;
    }
};

