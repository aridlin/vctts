#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace phonomenal_splicer {

enum class UnitKind {
  ExactChunk,
  ExactWord,
  WordPiece,
  Phoneme,
};

struct PlannedUnit {
  UnitKind kind{};
  std::string target_text;
  std::string text;
  std::vector<std::string> phones;
  std::string clip_id;
  std::int64_t start_sample{};
  std::int64_t end_sample{};
  std::int64_t word_group{-1};
  std::optional<double> confidence;
  int pause_before_ms = 0;
  bool approximated = false;
  std::int64_t end_word_group{-1};
};

struct PlanResult {
  std::string merc;
  std::string input_text;
  std::string normalized_text;
  std::filesystem::path master_audio_path;
  std::int64_t sample_rate{};
  std::vector<PlannedUnit> units;
  std::vector<std::string> warnings;
  double score = 0;
};

struct BoundaryCheck {
  std::string kind, label, edge, clip_id;
  std::int64_t source_sample{}, output_sample{};
  double sample_jump{};
  bool suspicious = false;
  std::uint32_t source_uncertainty_samples{}, independent_disputes{}, manual_review{};
};
struct PitchAdjustment {
  std::string clip_id;
  double source_hz{}, target_hz{}, factor{1};
  std::int64_t start_sample{}, end_sample{};
};
struct BoundaryReport {
  std::vector<BoundaryCheck> boundaries;
  std::vector<PitchAdjustment> pitch_adjustments;
  std::size_t flagged{};
  std::size_t pitch_skipped{};
  std::size_t word_pauses{};
  std::size_t duration_adjustments{};
  std::int64_t word_pause_samples{};
};
std::string BoundaryReportJson(const BoundaryReport &report);

struct SynthesizeOptions {
  std::int64_t max_chunk_words = 256;
  std::int64_t max_phone_ngram = 12;
  std::int64_t crossfade_ms = 4;
  std::int64_t word_gap_ms = 0;
  bool strict = false;
  double pitch_floor_hz = 0., pitch_ceiling_hz = 0.;
  std::size_t beam_width = 32;
  std::stop_token stop_token;
  bool adaptive_word_gaps = true;
  bool normalize_duration = false;
};

class VoiceBank {
public:
  static VoiceBank Load(const std::filesystem::path &bank_path);

  [[nodiscard]] const std::string &merc() const noexcept;
  [[nodiscard]] const std::string &quality_report() const noexcept;
  [[nodiscard]] std::int64_t sample_rate() const noexcept;
  [[nodiscard]] const std::filesystem::path &master_audio_path() const noexcept;

  [[nodiscard]] PlanResult
  PlanText(std::string_view text, const SynthesizeOptions &options = {}) const;

  [[nodiscard]] PlanResult
  PlanPhonemes(const std::vector<std::string> &phones,
               const SynthesizeOptions &options = {}) const;

  [[nodiscard]] std::vector<std::uint8_t>
  SynthesizeWav(const PlanResult &plan,
                const SynthesizeOptions &options = {}, BoundaryReport *audit = nullptr) const;

private:
  VoiceBank() = default;

  struct Impl;
  Impl *impl_ = nullptr;

public:
  VoiceBank(VoiceBank &&other) noexcept;
  VoiceBank &operator=(VoiceBank &&other) noexcept;
  VoiceBank(const VoiceBank &) = delete;
  VoiceBank &operator=(const VoiceBank &) = delete;
  ~VoiceBank();
};

std::string UnitKindToString(UnitKind kind);

} // namespace phonomenal_splicer
