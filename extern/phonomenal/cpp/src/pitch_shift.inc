// Pitch-synchronous overlap-add on reliable vowel interiors. Source waveform
// grains are copied without resampling, retaining their spectral envelope.
// Consonants, phone boundaries, sample count and untrusted regions stay intact.
static bool ShiftVowelPitch(std::vector<std::int16_t> &segment,
                           std::int64_t segment_start, const Phoneme &phone,
                           double factor, std::int64_t rate) {
  const auto &a=phone.acoustic;
  if(a.marks.size()<5 || a.voiced<.8 || a.spread>2.5 || a.clipping>=.01 ||
     phone.start_sample<segment_start || phone.end_sample>segment_start+static_cast<std::int64_t>(segment.size())) return false;
  const auto begin=phone.start_sample-segment_start;
  const auto count=phone.end_sample-phone.start_sample;
  std::vector<double> sum(count,0.),weights(count,0.);
  const auto &marks=a.marks;
  double destination=static_cast<double>(marks[1]-phone.start_sample);
  std::size_t source=1;
  while(destination<marks[marks.size()-2]-phone.start_sample) {
    const double absolute=phone.start_sample+destination;
    while(source+2<marks.size() && std::abs(marks[source+1]-absolute)<std::abs(marks[source]-absolute)) ++source;
    const double left=marks[source]-marks[source-1];
    const double right=marks[source+1]-marks[source];
    const double period=(left+right)*.5;
    if(period<rate/600. || period>rate/50. || left/right<.7 || left/right>1.4) return false;
    const auto centre=std::llround(destination);
    for(std::int64_t j=-static_cast<std::int64_t>(left);j<=static_cast<std::int64_t>(right);++j) {
      const auto dst=centre+j;
      const auto src=marks[source]-segment_start+j;
      if(dst<0 || dst>=count || src<begin || src>=begin+count) continue;
      const double phase=j/(j<0?left:right);
      const double w=.5+.5*std::cos(3.141592653589793*phase);
      sum[dst]+=segment[src]*w;weights[dst]+=w;
    }
    destination+=period/factor;
  }
  const auto guard=std::max<std::int64_t>(rate*.008,marks[1]-phone.start_sample);
  const auto tail=std::min<std::int64_t>(count-rate*.008,marks[marks.size()-2]-phone.start_sample);
  const double fade=std::max(rate*.010,2.*rate/a.pitch);
  if(tail-guard<rate*.020) return false;
  for(std::int64_t i=guard;i<tail;++i) {
    if(weights[i]<.2) continue;
    const double mix=std::clamp(std::min((i-guard)/fade,(tail-i)/fade),0.,1.);
    segment[begin+i]=ClampI16(segment[begin+i]*(1.-mix)+sum[i]/weights[i]*mix);
  }
  return true;
}

// Stretch only a stable vowel interior. Epoch grains are never resampled.
// The returned knots map absolute source samples onto the changed segment.
static bool StretchVowel(std::vector<std::int16_t> &segment, std::int64_t start,
                         const Phoneme &p, double factor, std::int64_t rate,
                         std::vector<std::pair<std::int64_t,std::int64_t>> &knots) {
  const auto &a=p.acoustic;
  if(std::string(" AA AE AH AO AW AY EH ER EY IH IY OW OY UH UW ").find(" "+p.normalized_label+" ")==std::string::npos) return false;
  if(a.marks.size()<5 || a.voiced<.8 || a.spread>2.5 || a.clipping>=.01) return false;
  const auto begin=p.start_sample-start, end=p.end_sample-start;
  const auto left=std::max<std::int64_t>(begin+rate*.012,a.marks[1]-start);
  const auto right=std::min<std::int64_t>(end-rate*.012,a.marks[a.marks.size()-2]-start);
  if(left<0 || end>static_cast<std::int64_t>(segment.size()) || right-left<rate*.025) return false;
  const auto length=std::llround((right-left)*factor), delta=length-(right-left);
  if(std::abs(delta)<rate*.005) return false;
  std::vector<double> sum(length), weight(length);
  double destination=0.; std::size_t source=1;
  while(destination<length) {
    const double absolute=start+left+destination/factor;
    while(source+2<a.marks.size() && std::abs(a.marks[source+1]-absolute)<std::abs(a.marks[source]-absolute)) ++source;
    const double l=a.marks[source]-a.marks[source-1], r=a.marks[source+1]-a.marks[source];
    const double period=(l+r)*.5;
    if(period<rate/600. || period>rate/50. || l/r<.7 || l/r>1.4) return false;
    for(std::int64_t j=-static_cast<std::int64_t>(l);j<=static_cast<std::int64_t>(r);++j) {
      const std::int64_t dst=std::llround(destination)+j, src=a.marks[source]-start+j;
      if(dst<0 || dst>=length || src<begin || src>=end) continue;
      const double phase=j/(j<0?l:r), w=.5+.5*std::cos(3.141592653589793*phase);
      sum[dst]+=segment[src]*w; weight[dst]+=w;
    }
    destination+=period;
  }
  if(std::any_of(weight.begin(),weight.end(),[](double w){return w<.1;})) return false;
  std::vector<std::int16_t> changed;changed.reserve(segment.size()+delta);
  changed.insert(changed.end(),segment.begin(),segment.begin()+left);
  const double fade=std::min<double>(rate*.010,length/4.);
  for(std::int64_t i=0;i<length;++i) {
    const double mix=std::clamp(std::min(i/fade,(length-1-i)/fade),0.,1.);
    const auto reference=i<length/2 ? left+i : right-length+i;
    changed.push_back(ClampI16(sum[i]/weight[i]*mix+segment[std::clamp<std::int64_t>(reference,left,right-1)]*(1.-mix)));
  }
  changed.insert(changed.end(),segment.begin()+right,segment.end());
  // Called from right to left: subsequent changes are strictly earlier.
  for(auto &k:knots) if(k.first>=start+right) k.second+=delta;
  knots.emplace_back(start+left,left);knots.emplace_back(start+right,left+length);
  segment=std::move(changed);return true;
}
