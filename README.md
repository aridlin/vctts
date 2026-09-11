# VOICECHAT TTS

A lightweight Windows overlay that lets you type a short message and play it over **two output devices at once**.  
Designed for games and voice chat where you need **fast TTS** without touching your microphone.

---

## Features

- **Offline Windows TTS** using system voices (WinRT / SAPI).
  - Uses installed Windows voices (e.g. *Adam, Zira, David*, etc.).
  - Voice selectable from the config menu.
- **Optional online keyless TTS fallback** (StreamElements).
- **Translator mode** using an online translation pass before speech synthesis, with target-language voice preference.
- **Incoming app setup** with target executable and optional per-app mute while the listener pipeline is running.
- **Dual output device playback** (play to two speakers / virtual cables simultaneously).
- **Reliable keyboard input** (proper layout + dead-key handling via `WM_CHAR`).
- **Global hotkeys** for quick start/stop while in-game.
- **Custom Win32 UI backend** inspired by `ftui`, with painted controls and recording overlay.
- **No microphone interference** – pure playback.

---

## Requirements

- **Windows 10 / 11**
- **Visual Studio 2022** or newer (Desktop C++ workload)
- **CMake 3.20+**
- A C++20-compatible compiler (MSVC or clang-cl)

---

## Dependencies

All required third-party libraries are **already included** in the repository:

```
extern/
miniaudio/
```

No external downloads or package managers required.

---

## Build (Windows)

### Using CMake + Ninja (recommended)

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

## Phonomenal voices

Choose **Phonomenal — choose voice pack…** in the voice selector and select a
`.vcpack` or legacy `.phbank` file. Selecting the Phonomenal entry again changes
the pack. A `.vcpack` contains its audio; `.phbank` needs its referenced WAV file
next to it or at the path recorded in the bank. Voice recordings are not bundled.

Speech uses the current native Phonomenal planner and splicer, including pitch
handling and adaptive word gaps. Packs load on the speech worker and are cached
for subsequent messages. The existing test button, playback devices, translation
and microphone bridge work with the resulting WAV. A pack error is shown
explicitly rather than silently switching to a system voice. Selecting a regular
voice or Custom switches back to that backend.

The vendored engine is pinned to commit
`73b7f53af25349ef8d4c1b7d4ffe4421eca9b6a4`; provenance and its MIT license are in
[extern/phonomenal](extern/phonomenal/README.md). No Python service is required.
