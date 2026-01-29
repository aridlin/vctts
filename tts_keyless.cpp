#include "tts_keyless.h"

#include <windows.h>
#include <winhttp.h>

#include <string>
#include <vector>

namespace
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

    std::string UrlEncode(const std::string& in)
    {
        static const char* hex = "0123456789ABCDEF";
        std::string out;
        out.reserve(in.size() * 3);
        for (unsigned char c : in)
        {
            if ((c >= 'A' && c <= 'Z') ||
                (c >= 'a' && c <= 'z') ||
                (c >= '0' && c <= '9') ||
                c == '-' || c == '_' || c == '.' || c == '~')
            {
                out.push_back((char)c);
            }
            else
            {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0xF]);
            }
        }
        return out;
    }
}

namespace tts_keyless
{
    std::vector<std::uint8_t> speak_to_audio_memory(const std::wstring& text, const std::wstring& voice)
    {
        std::vector<std::uint8_t> out;
        if (text.empty()) return out;

        std::string textUtf8 = WideToUtf8(text);
        std::string voiceUtf8 = WideToUtf8(voice.empty() ? L"Brian" : voice);

        std::string path = "/kappa/v2/speech";
        std::wstring pathW = Utf8ToWide(path);
        std::string body = "voice=" + UrlEncode(voiceUtf8) +
                           "&text=" + UrlEncode(textUtf8);
        std::wstring contentType = L"Content-Type: application/x-www-form-urlencoded\r\n";

        HINTERNET session = WinHttpOpen(L"TTSOverlay/1.0",
                                        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME,
                                        WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return out;

        HINTERNET connect = WinHttpConnect(session, L"api.streamelements.com",
                                           INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect)
        {
            WinHttpCloseHandle(session);
            return out;
        }

        HINTERNET request = WinHttpOpenRequest(connect, L"POST", pathW.c_str(),
                                               nullptr, WINHTTP_NO_REFERER,
                                               WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
        if (!request)
        {
            WinHttpCloseHandle(connect);
            WinHttpCloseHandle(session);
            return out;
        }

        BOOL ok = WinHttpSendRequest(request,
                                     contentType.c_str(),
                                     (DWORD)contentType.size(),
                                     (LPVOID)body.data(),
                                     (DWORD)body.size(),
                                     (DWORD)body.size(),
                                     0);
        if (ok) ok = WinHttpReceiveResponse(request, nullptr);

        if (ok)
        {
            DWORD statusCode = 0;
            DWORD statusSize = sizeof(statusCode);
            if (!WinHttpQueryHeaders(request,
                                     WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                     WINHTTP_HEADER_NAME_BY_INDEX,
                                     &statusCode,
                                     &statusSize,
                                     WINHTTP_NO_HEADER_INDEX) ||
                statusCode < 200 || statusCode >= 300)
            {
                WinHttpCloseHandle(request);
                WinHttpCloseHandle(connect);
                WinHttpCloseHandle(session);
                return out;
            }

            for (;;)
            {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                    break;

                size_t oldSize = out.size();
                out.resize(oldSize + available);
                DWORD read = 0;
                if (!WinHttpReadData(request, out.data() + oldSize, available, &read) || read == 0)
                {
                    out.resize(oldSize);
                    break;
                }
                if (read < available)
                    out.resize(oldSize + read);
            }
        }

        WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
}
