#pragma once

#include <string>

namespace app_audio_control
{
    int set_process_mute_by_exe(const std::wstring& exeName, bool muted);
}
