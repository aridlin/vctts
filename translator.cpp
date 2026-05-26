#include "translator.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    std::string wide_to_utf8(const std::wstring& s)
    {
        if (s.empty()) return {};
        int len = WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0, nullptr, nullptr);
        std::string out(len, '\0');
        WideCharToMultiByte(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len, nullptr, nullptr);
        return out;
    }

    std::wstring utf8_to_wide(const std::string& s)
    {
        if (s.empty()) return {};
        int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
        std::wstring out(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), out.data(), len);
        return out;
    }

    std::string url_encode(const std::string& s)
    {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string out;
        out.reserve(s.size() * 3);
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                out.push_back((char)c);
            } else {
                out.push_back('%');
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 15]);
            }
        }
        return out;
    }

    std::string normalize_lang(std::string lang)
    {
        lang.erase(std::remove_if(lang.begin(), lang.end(), [](unsigned char c) {
            return std::isspace(c) != 0;
        }), lang.end());
        std::transform(lang.begin(), lang.end(), lang.begin(), [](unsigned char c) {
            return (char)std::tolower(c);
        });
        if (lang.empty()) lang = "en";
        return lang;
    }

    std::string http_get_translate(const std::wstring& text, const std::string& target)
    {
        std::string q = url_encode(wide_to_utf8(text));
        std::wstring path = utf8_to_wide(
            "/translate_a/single?client=gtx&sl=auto&tl=" + normalize_lang(target) +
            "&dt=t&q=" + q);

        HINTERNET session = WinHttpOpen(L"vctts/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session) return {};

        HINTERNET connect = WinHttpConnect(session, L"translate.googleapis.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
        if (!connect) {
            WinHttpCloseHandle(session);
            return {};
        }

        HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
        std::string body;
        if (request &&
            WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                               WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
            WinHttpReceiveResponse(request, nullptr)) {
            DWORD available = 0;
            while (WinHttpQueryDataAvailable(request, &available) && available > 0) {
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0)
                    break;
                chunk.resize(read);
                body += chunk;
            }
        }

        if (request) WinHttpCloseHandle(request);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return body;
    }

    std::string unescape_json_string(const std::string& s)
    {
        std::string out;
        out.reserve(s.size());
        for (size_t i = 0; i < s.size(); ++i) {
            if (s[i] != '\\' || i + 1 >= s.size()) {
                out.push_back(s[i]);
                continue;
            }
            char n = s[++i];
            switch (n) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u':
                // Google responses for normal text are UTF-8 most of the time; keep
                // unsupported escapes as a visible placeholder instead of corrupting.
                if (i + 4 < s.size()) i += 4;
                out.push_back('?');
                break;
            default:
                out.push_back(n);
                break;
            }
        }
        return out;
    }

    std::vector<std::string> first_translation_segments(const std::string& json)
    {
        std::vector<std::string> parts;
        size_t pos = 0;
        while ((pos = json.find("[\"", pos)) != std::string::npos) {
            pos += 2;
            std::string part;
            bool escaped = false;
            for (; pos < json.size(); ++pos) {
                char c = json[pos];
                if (!escaped && c == '"') break;
                if (!escaped && c == '\\') {
                    escaped = true;
                    part.push_back(c);
                    continue;
                }
                escaped = false;
                part.push_back(c);
            }
            if (!part.empty()) parts.push_back(unescape_json_string(part));
            if (json.compare(pos, 4, "\",\"") != 0) break;
            pos += 1;
        }
        return parts;
    }

    std::string detected_language(const std::string& json)
    {
        int depth = 0;
        bool inString = false;
        bool escaped = false;
        for (size_t i = 0; i < json.size(); ++i) {
            char c = json[i];
            if (inString) {
                if (escaped) escaped = false;
                else if (c == '\\') escaped = true;
                else if (c == '"') inString = false;
                continue;
            }
            if (c == '"') inString = true;
            else if (c == '[') ++depth;
            else if (c == ']') --depth;
            else if (depth == 0 && c == ',') {
                size_t q1 = json.find('"', i + 1);
                size_t q2 = q1 == std::string::npos ? q1 : json.find('"', q1 + 1);
                if (q1 != std::string::npos && q2 != std::string::npos)
                    return json.substr(q1 + 1, q2 - q1 - 1);
                break;
            }
        }
        return {};
    }
}

namespace translator
{
    Result translate_auto(const std::wstring& text, const std::string& targetLanguage)
    {
        Result result;
        if (text.empty()) return result;

        std::string body = http_get_translate(text, targetLanguage);
        if (body.empty()) return result;

        std::ostringstream combined;
        for (const std::string& part : first_translation_segments(body))
            combined << part;

        result.text = utf8_to_wide(combined.str());
        result.detectedLanguage = detected_language(body);
        return result;
    }

    bool language_matches(const std::string& detected, const std::string& expected)
    {
        std::string a = normalize_lang(detected);
        std::string b = normalize_lang(expected);
        return a == b || a.rfind(b + "-", 0) == 0 || b.rfind(a + "-", 0) == 0;
    }
}
