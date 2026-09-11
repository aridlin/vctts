// Included inside the implementation namespace; wire format is specified in
// docs/vcpack_format.md.
struct FeatureFrame {
  float pitch{}, voicing{}, rms{};
  std::array<float, 8> spectrum{};
};
struct PackedData {
  std::string voice, language, report;
  WaveData wave;
  std::vector<Record> records;
  std::unordered_map<std::string, std::vector<std::string>> lexicon;
  std::uint32_t hop{};
  std::vector<FeatureFrame> features;
};
struct BinaryReader {
  std::span<const std::uint8_t> data;
  std::size_t pos{};
  void require(std::size_t n) const {
    if (pos > data.size() || n > data.size() - pos)
      throw std::runtime_error("Truncated vcpack section");
  }
  std::uint64_t integer(std::size_t n) {
    require(n);
    std::uint64_t v = 0;
    for (std::size_t i = 0; i < n; ++i)
      v |= std::uint64_t(data[pos++]) << (8 * i);
    return v;
  }
  std::uint32_t u32() { return static_cast<std::uint32_t>(integer(4)); }
  std::uint64_t u64() { return integer(8); }
  float f32() { return std::bit_cast<float>(u32()); }
  std::string string() {
    const auto n = u32();
    if (n > 1024 * 1024)
      throw std::runtime_error("vcpack string exceeds limit");
    require(n);
    std::string s(reinterpret_cast<const char *>(data.data() + pos), n);
    pos += n;
    if (s.find('\0') != std::string::npos)
      throw std::runtime_error("Embedded NUL in vcpack string");
    return s;
  }
  std::uint32_t count(std::uint32_t maximum = 10000000) {
    auto n = u32();
    if (n > maximum || n > data.size() - pos)
      throw std::runtime_error("vcpack count exceeds limit");
    return n;
  }
  std::optional<double> confidence() {
    float v = f32();
    if (v == -1)
      return {};
    if (!std::isfinite(v) || v < 0 || v > 1)
      throw std::runtime_error("Invalid vcpack confidence");
    return v;
  }
  void end() const {
    if (pos != data.size())
      throw std::runtime_error("Unexpected trailing section data");
  }
};
std::uint32_t Crc32(std::span<const std::uint8_t> data) {
  static const auto table = [] {
    std::array<std::uint32_t, 256> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
      auto c = i;
      for (int b = 0; b < 8; ++b)
        c = (c >> 1) ^ ((c & 1) ? 0xedb88320u : 0u);
      t[i] = c;
    }
    return t;
  }();
  std::uint32_t crc = 0xffffffffu;
  for (auto b : data)
    crc = (crc >> 8) ^ table[(crc ^ b) & 255];
  return crc ^ 0xffffffffu;
}
PackedData ReadVcpack(const std::filesystem::path &path) {
  const auto size = std::filesystem::file_size(path);
  if (size < 24 || size > 2ULL * 1024 * 1024 * 1024)
    throw std::runtime_error("vcpack size outside supported limits");
  std::ifstream file(path, std::ios::binary);
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
  if (!file.read(reinterpret_cast<char *>(bytes.data()),
                 static_cast<std::streamsize>(size)))
    throw std::runtime_error("Could not read vcpack");
  BinaryReader head{bytes};
  if (head.u64() != 0x00004b4341504356ULL || head.u32() != 1)
    throw std::runtime_error("Unsupported vcpack version/magic");
  const auto count = head.u32();
  if (count < 3 || count > 32 || head.u64() != size)
    throw std::runtime_error("Invalid vcpack header");
  std::unordered_map<std::string, std::span<const std::uint8_t>> sections;
  std::uint64_t previous = 24 + 32 * count;
  for (std::uint32_t i = 0; i < count; ++i) {
    head.require(4);
    std::string tag(reinterpret_cast<const char *>(bytes.data() + head.pos), 4);
    head.pos += 4;
    auto flags = head.u32();
    auto offset = head.u64();
    auto length = head.u64();
    auto crc = head.u32();
    auto reserved = head.u32();
    if (flags > 1 || reserved || offset != previous || offset > size ||
        length > size - offset || sections.contains(tag))
      throw std::runtime_error("Invalid vcpack section directory");
    if (flags && tag != "META" && tag != "RECS" && tag != "AUDI" &&
        tag != "LEXI" && tag != "FEAT" && tag != "QARE")
      throw std::runtime_error("Unknown required vcpack section");
    auto data = std::span<const std::uint8_t>(bytes).subspan(
        static_cast<std::size_t>(offset), static_cast<std::size_t>(length));
    if (Crc32(data) != crc)
      throw std::runtime_error("vcpack checksum mismatch: " + tag);
    sections.emplace(tag, data);
    previous = offset + length;
  }
  if (previous != size || !sections.contains("META") ||
      !sections.contains("AUDI") || !sections.contains("RECS"))
    throw std::runtime_error("Incomplete vcpack");
  PackedData result;
  BinaryReader meta{sections.at("META")};
  result.voice = meta.string();
  result.language = meta.string();
  auto alphabet = meta.string();
  result.wave.sample_rate = meta.u32();
  meta.end();
  if (result.voice.empty() || alphabet != "arpabet" ||
      result.wave.sample_rate < 8000 || result.wave.sample_rate > 192000)
    throw std::runtime_error("Unsupported vcpack audio/phone metadata");
  BinaryReader audio{sections.at("AUDI")};
  if (audio.data.empty() || audio.data.size() % 2)
    throw std::runtime_error("Invalid PCM16 section");
  result.wave.samples.resize(audio.data.size() / 2);
  for (auto &s : result.wave.samples)
    s = std::bit_cast<std::int16_t>(
        static_cast<std::uint16_t>(audio.integer(2)));
  auto range = [&](auto start, auto end) {
    if (start < 0 || end <= start ||
        static_cast<std::uint64_t>(end) > result.wave.samples.size())
      throw std::runtime_error("Invalid vcpack sample interval");
  };
  BinaryReader recs{sections.at("RECS")};
  auto nr = recs.count(1000000);
  std::unordered_map<std::string, bool> ids;
  for (std::uint32_t i = 0; i < nr; ++i) {
    Record record;
    record.clip_id = recs.string();
    auto transcript = recs.string();
    if (record.clip_id.empty() || !ids.emplace(record.clip_id, true).second)
      throw std::runtime_error("Invalid/duplicate record ID");
    auto nw = recs.count(100000);
    std::int64_t previous_end = 0;
    for (std::uint32_t j = 0; j < nw; ++j) {
      Word w;
      w.text = recs.string();
      w.start_sample = static_cast<std::int64_t>(recs.u64());
      w.end_sample = static_cast<std::int64_t>(recs.u64());
      w.confidence = recs.confidence();
      range(w.start_sample, w.end_sample);
      if (w.text.empty() || w.start_sample < previous_end)
        throw std::runtime_error("Invalid word sequence");
      previous_end = w.end_sample;
      record.words.push_back(std::move(w));
    }
    auto np = recs.count(1000000);
    previous_end = 0;
    for (std::uint32_t j = 0; j < np; ++j) {
      Phoneme p;
      p.label = recs.string();
      p.normalized_label = NormalizePhoneLabel(p.label);
      p.word_index = static_cast<std::int64_t>(recs.u32());
      p.start_sample = static_cast<std::int64_t>(recs.u64());
      p.end_sample = static_cast<std::int64_t>(recs.u64());
      p.confidence = recs.confidence();
      range(p.start_sample, p.end_sample);
      if (p.word_index < 0 ||
          static_cast<std::size_t>(p.word_index) >= record.words.size() ||
          p.normalized_label.empty() || p.start_sample < previous_end)
        throw std::runtime_error("Invalid phone sequence");
      const auto &w = record.words[static_cast<std::size_t>(p.word_index)];
      if (p.start_sample < w.start_sample || p.end_sample > w.end_sample)
        throw std::runtime_error("Phone outside word");
      previous_end = p.end_sample;
      record.phonemes.push_back(std::move(p));
    }
    if (record.words.empty() || record.phonemes.empty())
      throw std::runtime_error("Empty aligned record");
    result.records.push_back(std::move(record));
  }
  recs.end();
  if (result.records.empty())
    throw std::runtime_error("Empty voice pack");
  if (sections.contains("LEXI")) {
    BinaryReader lex{sections.at("LEXI")};
    auto n = lex.count(500000);
    for (std::uint32_t i = 0; i < n; ++i) {
      auto word = lex.string();
      auto nphones = lex.count(256);
      std::vector<std::string> phones;
      for (std::uint32_t j = 0; j < nphones; ++j)
        phones.push_back(lex.string());
      result.lexicon.emplace(std::move(word), std::move(phones));
    }
    lex.end();
  }
  if (sections.contains("FEAT")) {
    BinaryReader feat{sections.at("FEAT")};
    result.hop = feat.u32();
    auto dims = feat.u32();
    auto n = feat.u64();
    if (!result.hop || dims != 11 || n > (feat.data.size() - feat.pos) / 44 ||
        n != (result.wave.samples.size() + result.hop - 1) / result.hop)
      throw std::runtime_error("Invalid feature dimensions");
    result.features.resize(static_cast<std::size_t>(n));
    for (auto &f : result.features) {
      f.pitch = feat.f32();
      f.voicing = feat.f32();
      f.rms = feat.f32();
      for (auto &s : f.spectrum)
        s = feat.f32();
      if (!std::isfinite(f.pitch) || !std::isfinite(f.voicing) ||
          !std::isfinite(f.rms) || f.pitch < 0 || f.voicing < 0 ||
          f.voicing > 1 || f.rms < 0)
        throw std::runtime_error("Invalid acoustic feature");
      for (auto v : f.spectrum)
        if (!std::isfinite(v))
          throw std::runtime_error("Invalid spectrum");
    }
    feat.end();
  }
  if (sections.contains("ACOU")) {
    BinaryReader ac{sections.at("ACOU")};
    const auto version = ac.u32();
    // Optional versions unknown to this reader are ignored after CRC checking.
    if (version == 1 || version == 2) {
      if (ac.u32() != result.records.size())
        throw std::runtime_error("ACOU recording count differs from RECS");
      for (auto &r : result.records) {
        if (ac.string() != r.clip_id || ac.u32() != r.phonemes.size())
          throw std::runtime_error("ACOU recording identity differs from RECS");
        for (auto &p : r.phonemes) {
          auto &a = p.acoustic;
          a.available = true;
          a.pitch=ac.f32(); a.spread=ac.f32(); a.voiced=ac.f32(); a.rms=ac.f32();
          a.clipping=ac.f32(); a.silence=ac.f32(); a.onset=ac.f32(); a.offset=ac.f32(); a.duration_z=ac.f32();
          for (auto value : {a.pitch,a.spread,a.voiced,a.rms,a.clipping,a.silence,a.onset,a.offset,a.duration_z})
            if (!std::isfinite(value)) throw std::runtime_error("Nonfinite ACOU measurement");
          if (a.pitch<0 || a.pitch>2000 || a.spread<0 || a.voiced<0 || a.voiced>1 ||
              a.rms<0 || a.rms>1 || a.clipping<0 || a.clipping>1 || a.silence<0 || a.silence>1 ||
              a.onset<0 || a.onset>1 || a.offset<0 || a.offset>1)
            throw std::runtime_error("Invalid ACOU measurement");
          const auto count=ac.u32();
          if(version>=2) {
            a.typical_ms=ac.f32();a.duration_scale=ac.f32();a.duration_examples=ac.u32();
            if(!std::isfinite(a.typical_ms) || !std::isfinite(a.duration_scale) || a.typical_ms<=0 || a.duration_scale<=0)
              throw std::runtime_error("Invalid duration statistics");
          }
          if (count > (ac.data.size()-ac.pos)/8 || count > static_cast<std::uint64_t>(p.end_sample-p.start_sample))
            throw std::runtime_error("Invalid pitch mark count");
          std::int64_t previous = p.start_sample-1;
          for (std::uint32_t i=0;i<count;++i) {
            const auto mark=ac.u64();
            if (mark>=static_cast<std::uint64_t>(p.end_sample) || (previous>=0 && mark<=static_cast<std::uint64_t>(previous)) ||
                mark<static_cast<std::uint64_t>(p.start_sample))
              throw std::runtime_error("Invalid source pitch mark");
            previous=static_cast<std::int64_t>(mark);a.marks.push_back(previous);
          }
        }
      }
      ac.end();
    }
  }
  if (sections.contains("QREV")) {
    BinaryReader qr{sections.at("QREV")};
    if(qr.u32()==1) {
      if(qr.u32()!=result.records.size()) throw std::runtime_error("QREV record count differs from RECS");
      for(auto &r:result.records) {
        if(qr.string()!=r.clip_id || qr.u32()!=r.phonemes.size()) throw std::runtime_error("QREV identity differs from RECS");
        for(auto &p:r.phonemes) {p.manual_review=qr.u32();if(p.manual_review>2) throw std::runtime_error("Invalid manual review");}
      }
      qr.end();
    }
  }
  if (sections.contains("TRST")) {
    BinaryReader tr{sections.at("TRST")};
    if(tr.u32()==1) {
      if(tr.u32()!=result.records.size()) throw std::runtime_error("TRST record count differs from RECS");
      for(auto &r:result.records) {
        if(tr.string()!=r.clip_id || tr.u32()!=r.phonemes.size()) throw std::runtime_error("TRST identity differs from RECS");
        for(auto &p:r.phonemes) {
          p.phone_support=tr.u32();p.phone_dispute=tr.u32();p.word_support=tr.u32();p.word_dispute=tr.u32();p.boundary_radius=tr.u32();
          if(p.phone_support>32 || p.phone_dispute>32 || p.word_support>32 || p.word_dispute>32 || p.boundary_radius>result.wave.samples.size())
            throw std::runtime_error("Invalid recognition evidence");
        }
      }
      tr.end();
    }
  }
  if (sections.contains("QARE")) {
    auto s = sections.at("QARE");
    result.report.assign(reinterpret_cast<const char *>(s.data()), s.size());
  }
  return result;
}
