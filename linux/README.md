# Native Linux VCTTS

Ctrl+Backspace opens the frameless typing overlay above other windows, including
fullscreen windows on KDE Wayland. Enter or the same shortcut submits speech;
Escape discards the text. The default idle timeout submits after 30 seconds.
Ctrl+Shift+Backspace opens configuration. Ctrl+Alt+Backspace stops speech.
All three shortcuts can be changed under Advanced settings.

The overlay uses native Wayland layer-shell with exclusive keyboard focus only
while typing. Hiding it releases keyboard focus to the previous app. Playback
status uses a separate non-interactive overlay. X11 uses a topmost frameless
window. KDE GlobalAccel handles the shortcuts. Closing configuration or clicking
Done keeps VCTTS in the tray; use the tray menu to quit, type, configure, stop,
or replay one of the last ten messages. Recent messages live only in memory.
A second launch opens the running instance instead of starting another copy.
Use `--background` to start directly in the tray.

The compact configuration contains voice, headphone output, a dedicated voice-chat toggle, physical microphone
on/off and mute, and a test message. Advanced settings hides the less common
controls: voice-pack path, custom program, speech rate, translation, explicit
online fallback, physical microphone selection, mic gain, an extra output, separate playback
volumes, virtual audio repair, timeout, and editable global shortcuts.

The program creates a VCTTS Chat Audio sink and VCTTS Virtual Microphone source
without changing system default devices. Choose VCTTS Virtual Microphone in the
voice-chat app. The optional microphone bridge mixes the selected physical mic
into that source; mute affects only the bridge. The voice-chat toggle sends speech directly to the VCTTS bus independently
of the listening/extra output selections. Headphone selections cannot disconnect
voice chat. Extra output selection is under Advanced settings.

Speech backends: offline Microsoft Zira Pro (English) and Paulina (Polish)
through a persistent Microsoft Speech Platform 11 COM worker under Wine;
English/Polish eSpeak NG as an explicit alternative; the original Phonomenal
.vcpack/.phbank engine, StreamElements Brian (explicit online selection), and a
custom program receiving text on stdin and returning WAV on stdout. Custom
programs use argument arrays; message text is not shell code. Stop terminates
an external synthesis process promptly. New input replaces pending synthesis.
Optional outgoing Google translation and online Brian fallback are disabled by
default. An invalid Phonomenal pack or custom program reports an explicit error.

The Microsoft backend uses the actual Microsoft Speech Platform COM runtime
and voice engines, not eSpeak. It requires Wine and a private prefix at
`~/.local/share/vctts/sapi/prefix`, with the official Microsoft Speech Platform 11
x64 runtime and ZiraPro/Paulina MSI packages installed. The Windows helper is
`~/.local/share/vctts/sapi/speech11-bridge.exe`. It returns WAV via framed local
pipes; it has no network dependency. A cold Wine startup may take a few seconds;
the process stays warm. Test override: `VCTTS_SAPI_ROOT`.
Windows 10/11 desktop voice compatibility is separate from Speech Platform 11.
Windows virtual-audio drivers are replaced by the Linux virtual microphone.
Incoming speech transcription/translation is not implemented; the Windows
incoming-listener worker also does not implement transcription/translation.
Per-app muting from the Windows build is not implemented in this frontend.
Settings use QSettings for aridlin/VCTTS. Speech audio uses a private temporary
directory and previous utterance audio is removed when the next starts.

Build from repository root:
`cmake -S . -B build-linux && cmake --build build-linux -j4`
Dependencies: Qt6 Widgets, Multimedia, Network, Concurrent, Test; KF6 GlobalAccel
and WindowSystem; LayerShellQt; eSpeak NG; pactl and a PulseAudio-compatible
server (PipeWire supported). The `vctts-audio` helper must be installed in
`~/.local/bin/`.

Checks:
- `ctest --test-dir build-linux/linux --output-on-failure` checks Phonomenal.
- `vctts --test-overlay` checks the compact/advanced configuration, native layer,
  keyboard focus, Unicode input, editing, cancellation, timeout, Enter, playback.
- `vctts --test-cancel` checks interruption of a stalled custom TTS process.
- `vctts --test-audio` checks offline generation and VCTTS bus playback.

Run test modes with an isolated `XDG_CONFIG_HOME`; they change test selections.
`VCTTS_TEST_SCREENSHOT` optionally saves an overlay and config PNG during the
UI check. `VCTTS_TEST_MIC` optionally selects a silent synthetic microphone to
check bridge routing, 50 percent gain, and mute teardown.

`--test-configured-audio` verifies the selected voice and output configuration
without forcing a special virtual-output selection. Test modes use a separate
local instance socket. Use isolated settings and mute any live voice-chat
recording stream while generating test speech.


## Install on Linux

The tested desktop is KDE Plasma 6 on Wayland. Install Qt 6 Widgets, Multimedia,
Network, Concurrent and Test; KF6 GlobalAccel/WindowSystem; LayerShellQt; CMake;
a C++20 compiler; Python 3; pactl and eSpeak NG. Microsoft voices additionally
need Wine and MinGW's `x86_64-w64-mingw32-g++` to build the small COM worker.

```sh
cmake -S . -B build-linux -DCMAKE_BUILD_TYPE=Release
cmake --build build-linux -j4
ctest --test-dir build-linux/linux --output-on-failure
cmake --install build-linux --prefix "$HOME/.local"
```

Launch `~/.local/bin/vctts`. Closing settings leaves it in the tray. Choose
**VCTTS Virtual Microphone** in Discord's Input Device setting, and keep
**Send speech to Discord / virtual microphone** checked in VCTTS. Select your
headphones under Listen on. The extra playback output is under Advanced.

## Set up the offline Microsoft voices

The runtime/voice packages come from Microsoft's official download center:
[Speech Platform 11 runtime](https://www.microsoft.com/en-us/download/details.aspx?id=27225)
and [runtime languages](https://www.microsoft.com/en-us/download/details.aspx?id=27224).
This tested Wine configuration also needs the native Microsoft `sapi.dll` from
your own Windows installation; Wine's built-in SAPI is incomplete. The setup
script copies it read-only from the source into a new private prefix. Microsoft
binaries and voice data are not included in this repository or Linux artifacts.

After building/installing the COM worker, run:

```sh
python3 ~/.local/share/vctts/sapi/setup_microsoft_voices.py \
  --windows-root /path/to/Windows
```

`--windows-root` is the Windows directory itself, containing `System32`, rather
than the root of the Windows drive. The script downloads and installs the
Microsoft runtime, Zira Pro and Paulina in VCTTS's private prefix and lists the
available voices. Review Microsoft's package license terms before installation.
It refuses to overwrite an existing Wine prefix. Restart VCTTS after setup,
then choose Microsoft Zira Pro or Microsoft Paulina in the voice picker.
