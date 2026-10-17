// clang-format off
#include <windows.h>
#include <sapi.h>
// clang-format on
// Local Microsoft Speech Platform COM bridge. No network or audio-device
// access.
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <io.h>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
const GUID waveFormat = {0xc31adbae,
                         0x527f,
                         0x4ff5,
                         {0xa2, 0x30, 0xf6, 0x2b, 0xb6, 0x1f, 0xf7, 0x0c}};
template <class T> struct Com {
  T *p = nullptr;
  ~Com() {
    if (p)
      p->Release();
  }
  T *operator->() { return p; }
  T **out() { return &p; }
};
static CLSID cls(const wchar_t *s) {
  CLSID c;
  CLSIDFromString(s, &c);
  return c;
}
static std::wstring wide(const std::string &s) {
  int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), s.size(),
                              0, 0);
  std::wstring out(n, 0);
  if (n)
    MultiByteToWideChar(CP_UTF8, 0, s.data(), s.size(), out.data(), n);
  return out;
}
static std::string utf8(const wchar_t *s) {
  int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, 0, 0, 0, 0);
  std::string out(n, 0);
  WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, 0, 0);
  if (n)
    out.pop_back();
  return out;
}
static HRESULT enumerate(IEnumSpObjectTokens **result) {
  Com<ISpObjectTokenCategory> category;
  HRESULT h = CoCreateInstance(
      cls(L"{bf86f6c4-f9ce-441a-821a-415a76b86d79}"), 0, CLSCTX_INPROC_SERVER,
      IID_ISpObjectTokenCategory, (void **)category.out());
  if (FAILED(h)) {
    fprintf(stderr, "Microsoft bridge step %d: 0x%08lx\n", __LINE__,
            (unsigned long)h);
    return h;
  }
  h = category->SetId(
      L"HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Speech Server\\v11.0\\Voices",
      FALSE);
  if (FAILED(h)) {
    fprintf(stderr, "Microsoft bridge step %d: 0x%08lx\n", __LINE__,
            (unsigned long)h);
    return h;
  }
  return category->EnumTokens(0, 0, result);
}
static HRESULT synth(const std::wstring &name, const std::wstring &text,
                     const wchar_t *output, int rate) {
  Com<IEnumSpObjectTokens> tokens;
  HRESULT h = enumerate(tokens.out());
  if (FAILED(h)) {
    fprintf(stderr, "Microsoft bridge step %d: 0x%08lx\n", __LINE__,
            (unsigned long)h);
    return h;
  }
  Com<ISpObjectToken> selected;
  ULONG count = 0;
  tokens->GetCount(&count);
  for (ULONG i = 0; i < count; ++i) {
    ISpObjectToken *token = 0;
    tokens->Item(i, &token);
    LPWSTR desc = 0;
    token->GetStringValue(0, &desc);
    std::wstring label = desc ? desc : L"";
    CoTaskMemFree(desc);
    if (label.find(name) != std::wstring::npos) {
      selected.p = token;
      break;
    }
    token->Release();
  }
  if (!selected.p)
    return HRESULT_FROM_WIN32(ERROR_NOT_FOUND);
  Com<ISpVoice> voice;
  h = CoCreateInstance(cls(L"{d941651c-44e6-4c17-badf-c36826fc3424}"), 0,
                       CLSCTX_INPROC_SERVER, IID_ISpVoice,
                       (void **)voice.out());
  if (FAILED(h)) {
    fprintf(stderr, "Microsoft bridge step %d: 0x%08lx\n", __LINE__,
            (unsigned long)h);
    return h;
  }
  h = voice->SetVoice(selected.p);
  if (FAILED(h)) {
    fprintf(stderr, "Microsoft bridge step %d: 0x%08lx\n", __LINE__,
            (unsigned long)h);
    return h;
  }
  Com<ISpStream> stream;
  h = CoCreateInstance(cls(L"{0f59bf10-9fa9-4469-b4d3-d83d939b4ac3}"), 0,
                       CLSCTX_INPROC_SERVER, IID_ISpStream,
                       (void **)stream.out());
  if (FAILED(h)) {
    fprintf(stderr, "Microsoft bridge step %d: 0x%08lx\n", __LINE__,
            (unsigned long)h);
    return h;
  }
  WAVEFORMATEX format = {WAVE_FORMAT_PCM, 1, 24000, 48000, 2, 16, 0};
  h = stream->BindToFile(output, SPFM_CREATE_ALWAYS, &waveFormat, &format, 0);
  if (FAILED(h)) {
    fprintf(stderr, "Microsoft bridge step %d: 0x%08lx\n", __LINE__,
            (unsigned long)h);
    return h;
  }
  h = voice->SetOutput(stream.p, TRUE);
  if (SUCCEEDED(h)) {
    voice->SetRate(rate);
    h = voice->Speak(text.c_str(), SPF_IS_NOT_XML, 0);
  }
  voice->SetOutput(0, FALSE);
  stream->Close();
  return h;
}
static bool read32(unsigned &n) { return fread(&n, 4, 1, stdin) == 1; }
static bool readString(std::string &out, unsigned limit) {
  unsigned n;
  if (!read32(n) || n > limit)
    return false;
  out.resize(n);
  return !n || fread(out.data(), 1, n, stdin) == n;
}
static void response(unsigned code, const std::vector<char> &data) {
  unsigned n = data.size();
  fwrite(&code, 4, 1, stdout);
  fwrite(&n, 4, 1, stdout);
  if (n)
    fwrite(data.data(), 1, n, stdout);
  fflush(stdout);
}
int wmain(int argc, wchar_t **argv) {
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  HRESULT h = CoInitializeEx(0, COINIT_APARTMENTTHREADED);
  if (FAILED(h))
    return 1;
  if (argc == 2 && std::wstring(argv[1]) == L"--list") {
    Com<IEnumSpObjectTokens> tokens;
    h = enumerate(tokens.out());
    if (FAILED(h))
      return 2;
    ULONG n;
    tokens->GetCount(&n);
    for (ULONG i = 0; i < n; ++i) {
      Com<ISpObjectToken> t;
      tokens->Item(i, t.out());
      LPWSTR name = 0;
      t->GetStringValue(0, &name);
      if (name) {
        printf("%s\n", utf8(name).c_str());
        CoTaskMemFree(name);
      }
    }
    return 0;
  }
  if (argc == 2 && std::wstring(argv[1]) == L"--server") {
    std::string name, text;
    unsigned rate;
    while (readString(name, 256) && readString(text, 65536) && read32(rate)) {
      wchar_t directory[MAX_PATH], path[MAX_PATH];
      GetTempPathW(MAX_PATH, directory);
      GetTempFileNameW(directory, L"vct", 0, path);
      h = synth(wide(name), wide(text), path, (int)rate);
      std::vector<char> data;
      if (SUCCEEDED(h)) {
        std::ifstream file(path, std::ios::binary);
        data.assign(std::istreambuf_iterator<char>(file), {});
      }
      DeleteFileW(path);
      if (FAILED(h)) {
        char message[128];
        snprintf(message, sizeof message,
                 "Microsoft speech synthesis failed: 0x%08lx",
                 (unsigned long)h);
        data.assign(message, message + strlen(message));
      }
      response(FAILED(h) ? 1 : 0, data);
    }
    CoUninitialize();
    return 0;
  }
  if (argc < 3)
    return 2;
  std::string text((std::istreambuf_iterator<char>(std::cin)), {});
  h = synth(argv[1], wide(text), argv[2], argc > 3 ? _wtoi(argv[3]) : 0);
  if (FAILED(h))
    fprintf(stderr, "Microsoft speech failed: 0x%08lx\n", (unsigned long)h);
  CoUninitialize();
  return FAILED(h) ? 1 : 0;
}
