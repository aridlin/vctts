#pragma once

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
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

    // ---- Timings ----
    int timeoutSeconds = 30;
    double toggleDebounceSec = 0.35;
    std::chrono::steady_clock::time_point lastInput = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastToggle = std::chrono::steady_clock::now();

    // ---- Modifiers (hook-updated) ----
    std::atomic<bool> shift{false};
    std::atomic<bool> ctrl{false};
    std::atomic<bool> alt{false};

    // ---- Text buffer (Unicode) ----
    std::mutex bufMutex;
    std::wstring buffer;

    // ---- Audio device config (GUI-selected) ----
    std::vector<AudioDevice> outDevices;
    std::vector<std::string> outDevicesUtf8; // UI-friendly names (UTF-8)
    int devA = 0;
    int devB = 0;

    // ---- Buffer ops ----
    void clearBuffer() {
        std::lock_guard<std::mutex> lock(bufMutex);
        buffer.clear();
    }

    void appendChar(wchar_t c) {
        std::lock_guard<std::mutex> lock(bufMutex);
        buffer.push_back(c);
        lastInput = std::chrono::steady_clock::now();
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
        std::lock_guard<std::mutex> lock(const_cast<std::mutex&>(bufMutex));
        return buffer;
    }

    std::wstring takeBufferAndClear() {
        std::lock_guard<std::mutex> lock(bufMutex);
        std::wstring out = buffer;
        buffer.clear();
        return out;
    }

    static std::string sanitizePreview(const std::wstring& ws) {
        // ASCII passthrough, Polish diacritics -> %x style, unknown -> '?'
        std::string out;
        out.reserve(ws.size());

        auto emit = [&](const char* s) { out += s; };

        for (wchar_t c : ws) {
            if (c >= 32 && c < 127) {
                out.push_back(static_cast<char>(c));
                continue;
            }
            switch (c) {
                case L'ą': emit("%a"); break;
                case L'ć': emit("%c"); break;
                case L'ę': emit("%e"); break;
                case L'ł': emit("%l"); break;
                case L'ń': emit("%n"); break;
                case L'ó': emit("%o"); break;
                case L'ś': emit("%s"); break;
                case L'ż': emit("%z"); break;
                case L'ź': emit("%x"); break;

                case L'Ą': emit("%A"); break;
                case L'Ć': emit("%C"); break;
                case L'Ę': emit("%E"); break;
                case L'Ł': emit("%L"); break;
                case L'Ń': emit("%N"); break;
                case L'Ó': emit("%O"); break;
                case L'Ś': emit("%S"); break;
                case L'Ż': emit("%Z"); break;
                case L'Ź': emit("%X"); break;

                default: out.push_back('?'); break;
            }
        }
        return out;
    }
};

