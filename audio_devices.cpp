#include "audio_devices.h"
#include "audio_playback.h"

void RefreshOutputDevices(AppState& s)
{
    audio_playback::refresh_audio_devices(s);
}
