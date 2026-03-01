#include "custom_tts.h"
#include <windows.h>

namespace custom_tts
{
    std::string WideToUtf8(const std::wstring& s)
    {
        if (s.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
        return out;
    }

    std::vector<std::uint8_t> SpeakCustomCommand(const std::wstring& text, const char* cmdTemplate)
    {
        std::vector<std::uint8_t> out;
        if (!cmdTemplate || cmdTemplate[0] == '\0') return out;

        std::string templ = cmdTemplate;
        std::string u8Text = WideToUtf8(text);

        // Escape quotes in the text to prevent breaking the command line
        std::string escapedText;
        escapedText.reserve(u8Text.size() + 10);
        for (char c : u8Text) {
            if (c == '"') escapedText += "\\\"";
            else escapedText += c;
        }

        size_t pos = templ.find("{text}");
        while (pos != std::string::npos)
        {
            templ.replace(pos, 6, escapedText);
            pos = templ.find("{text}", pos + escapedText.length());
        }

        std::wstring cmdW = Utf8ToWide(templ);

        HANDLE hReadPipe, hWritePipe;
        SECURITY_ATTRIBUTES sa;
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        if (!CreatePipe(&hReadPipe, &hWritePipe, &sa, 0))
            return out;

        // Ensure the read handle is NOT inherited.
        SetHandleInformation(hReadPipe, HANDLE_FLAG_INHERIT, 0);

        STARTUPINFOW si;
        ZeroMemory(&si, sizeof(si));
        si.cb = sizeof(si);
        si.hStdOutput = hWritePipe;
        // We don't want to capture stderr to the same buffer unless intended.
        // If they write errors to stdout, it will corrupt the WAV.
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
        si.dwFlags |= STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof(pi));

        // CreateProcessW requires a mutable string buffer
        std::vector<wchar_t> cmdBuf(cmdW.begin(), cmdW.end());
        cmdBuf.push_back(L'\0');

        if (CreateProcessW(
                nullptr,            // Application name
                cmdBuf.data(),      // Command line
                nullptr,            // Process attributes
                nullptr,            // Thread attributes
                TRUE,               // Inherit handles
                CREATE_NO_WINDOW,   // Creation flags
                nullptr,            // Environment
                nullptr,            // Current directory
                &si,
                &pi))
        {
            // Close write pipe on our side so we can read until EOF.
            CloseHandle(hWritePipe);

            DWORD read;
            char buffer[4096];
            while (ReadFile(hReadPipe, buffer, sizeof(buffer), &read, nullptr) && read > 0)
            {
                out.insert(out.end(), buffer, buffer + read);
            }

            WaitForSingleObject(pi.hProcess, INFINITE);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        }
        else
        {
            CloseHandle(hWritePipe);
        }

        CloseHandle(hReadPipe);
        return out;
    }
}
