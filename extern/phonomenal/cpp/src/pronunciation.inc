// Native eSpeak NG pronunciation only. Audio always comes from the selected
// pack.
std::vector<std::string> NativePronunciation(const std::string &word) {
  using Init = int (*)(int, int, const char *, int);
  using Voice = int (*)(const char *);
  using Phones = const char *(*)(const void **, int, int);
  static std::mutex mutex;
  std::lock_guard guard(mutex);
  static Init initialize = nullptr;
  static Voice voice = nullptr;
  static Phones phones = nullptr;
  static bool attempted = false, ready = false;
  if (!attempted) {
    attempted = true;
#ifdef _WIN32
    auto module = LoadLibraryW(L"libespeak-ng.dll");
    if (!module)
      module = LoadLibraryW(L"espeak-ng.dll");
    if (module) {
      initialize =
          reinterpret_cast<Init>(GetProcAddress(module, "espeak_Initialize"));
      voice = reinterpret_cast<Voice>(
          GetProcAddress(module, "espeak_SetVoiceByName"));
      phones = reinterpret_cast<Phones>(
          GetProcAddress(module, "espeak_TextToPhonemes"));
    }
#else
    auto module = dlopen("libespeak-ng.so.1", RTLD_NOW | RTLD_LOCAL);
    if (module) {
      initialize = reinterpret_cast<Init>(dlsym(module, "espeak_Initialize"));
      voice = reinterpret_cast<Voice>(dlsym(module, "espeak_SetVoiceByName"));
      phones = reinterpret_cast<Phones>(dlsym(module, "espeak_TextToPhonemes"));
    }
#endif
    ready = initialize && voice && phones && initialize(2, 0, nullptr, 0) > 0;
  }
  if (!ready || voice("en-us") != 0)
    return {};
  const void *ptr = word.c_str();
  const char *raw = phones(&ptr, 1, '|' << 8);
  if (!raw)
    return {};
  static const std::unordered_map<std::string, std::vector<std::string>> map = {
      {"p", {"P"}},         {"b", {"B"}},        {"t", {"T"}},
      {"d", {"D"}},         {"k", {"K"}},        {"g", {"G"}},
      {"f", {"F"}},         {"v", {"V"}},        {"T", {"TH"}},
      {"D", {"DH"}},        {"s", {"S"}},        {"z", {"Z"}},
      {"S", {"SH"}},        {"Z", {"ZH"}},       {"h", {"HH"}},
      {"tS", {"CH"}},       {"dZ", {"JH"}},      {"m", {"M"}},
      {"n", {"N"}},         {"N", {"NG"}},       {"l", {"L"}},
      {"r", {"R"}},         {"R", {"R"}},        {"w", {"W"}},
      {"j", {"Y"}},         {"i:", {"IY"}},      {"i", {"IY"}},
      {"I", {"IH"}},        {"I2", {"IH"}},      {"E", {"EH"}},
      {"a", {"AE"}},        {"A:", {"AA"}},      {"A", {"AA"}},
      {"0", {"AA"}},        {"O:", {"AO"}},      {"O", {"AO"}},
      {"U", {"UH"}},        {"u:", {"UW"}},      {"u", {"UW"}},
      {"V", {"AH"}},        {"@", {"AH"}},       {"3:", {"ER"}},
      {"3", {"ER"}},        {"@r", {"ER"}},      {"@R", {"ER"}},
      {"aI", {"AY"}},       {"aU", {"AW"}},      {"eI", {"EY"}},
      {"oU", {"OW"}},       {"OI", {"OY"}},      {"@L", {"AH", "L"}},
      {"@2", {"AH"}},       {"I@", {"IH", "R"}}, {"i@", {"IY", "R"}},
      {"i@3", {"IY", "R"}}, {"e@", {"EH", "R"}}, {"U@", {"UH", "R"}},
      {"t#", {"T"}},        {"d#", {"D"}},       {"L", {"L"}}};
  std::string code;
  std::vector<std::string> out;
  bool valid = true;
  auto flush = [&] {
    if (code.empty())
      return;
    auto it = map.find(code);
    if (it == map.end()) {
      valid = false;
    } else
      out.insert(out.end(), it->second.begin(), it->second.end());
    code.clear();
  };
  for (char c : std::string(raw)) {
    if (c == '|' || std::isspace(static_cast<unsigned char>(c)))
      flush();
    else if (c != '\'' && c != ',' && c != '_' && c != '=')
      code += c;
  }
  flush();
  return valid ? out : std::vector<std::string>{};
}
