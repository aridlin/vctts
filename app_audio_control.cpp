#include "app_audio_control.h"

#include <windows.h>
#include <mmdeviceapi.h>
#include <audiopolicy.h>
#include <wrl/client.h>

#include <algorithm>
#include <cwctype>
#include <string>

using Microsoft::WRL::ComPtr;

namespace
{
    std::wstring basename_lower(std::wstring path)
    {
        size_t slash = path.find_last_of(L"\\/");
        if (slash != std::wstring::npos)
            path = path.substr(slash + 1);

        std::transform(path.begin(), path.end(), path.begin(), [](wchar_t c) {
            return (wchar_t)towlower(c);
        });
        return path;
    }

    std::wstring process_exe_name(DWORD pid)
    {
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
        if (!process) return {};

        wchar_t path[MAX_PATH]{};
        DWORD size = (DWORD)std::size(path);
        std::wstring out;
        if (QueryFullProcessImageNameW(process, 0, path, &size))
            out.assign(path, size);

        CloseHandle(process);
        return basename_lower(out);
    }
}

namespace app_audio_control
{
    int set_process_mute_by_exe(const std::wstring& exeName, bool muted)
    {
        std::wstring target = basename_lower(exeName);
        if (target.empty()) return 0;
        if (target.find(L'.') == std::wstring::npos)
            target += L".exe";

        HRESULT initHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool didInit = SUCCEEDED(initHr);
        if (FAILED(initHr) && initHr != RPC_E_CHANGED_MODE)
            return 0;

        int changed = 0;

        {
            ComPtr<IMMDeviceEnumerator> enumerator;
            if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))))
                goto cleanup;

            ComPtr<IMMDevice> device;
            if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)))
                goto cleanup;

            ComPtr<IAudioSessionManager2> manager;
            if (FAILED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr, &manager)))
                goto cleanup;

            ComPtr<IAudioSessionEnumerator> sessions;
            if (FAILED(manager->GetSessionEnumerator(&sessions)))
                goto cleanup;

            int count = 0;
            sessions->GetCount(&count);
            for (int i = 0; i < count; ++i)
            {
                ComPtr<IAudioSessionControl> session;
                if (FAILED(sessions->GetSession(i, &session)) || !session)
                    continue;

                ComPtr<IAudioSessionControl2> session2;
                if (FAILED(session.As(&session2)) || !session2)
                    continue;

                DWORD pid = 0;
                if (FAILED(session2->GetProcessId(&pid)) || pid == 0)
                    continue;

                if (process_exe_name(pid) != target)
                    continue;

                ComPtr<ISimpleAudioVolume> volume;
                if (SUCCEEDED(session.As(&volume)) && volume)
                {
                    if (SUCCEEDED(volume->SetMute(muted ? TRUE : FALSE, nullptr)))
                        ++changed;
                }
            }
        }

    cleanup:
        if (didInit)
            CoUninitialize();
        return changed;
    }
}
