#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace tts_phonomenal {
void select(const std::filesystem::path& path);
void disable();
bool enabled();
std::filesystem::path selected_path();
std::vector<std::uint8_t> speak(const std::string& text);
const std::string& last_error();
}
