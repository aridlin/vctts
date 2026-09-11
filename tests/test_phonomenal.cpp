#include "tts_phonomenal.h"
#include <bit>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string_view>
using Bytes=std::vector<std::uint8_t>;
static void number(Bytes& b,std::uint64_t n,unsigned count){for(unsigned i=0;i<count;++i)b.push_back(static_cast<std::uint8_t>(n>>(8*i)));}
static void raw(Bytes& b,std::string_view text){b.insert(b.end(),text.begin(),text.end());}
static void string(Bytes& b,std::string_view text){number(b,text.size(),4);raw(b,text);}
static std::uint32_t crc(const Bytes& bytes){std::uint32_t c=~0u;for(auto byte:bytes){c^=byte;for(int i=0;i<8;++i)c=(c>>1)^(0xedb88320u & (0u-(c&1)));}return ~c;}
static void write(const std::filesystem::path& path,const Bytes& bytes){std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()));assert(out);}
static Bytes audio(int frequency){Bytes samples;for(int i=0;i<4800;++i)number(samples,static_cast<std::uint16_t>(static_cast<std::int16_t>(8000*std::sin(i*frequency*6.283185307/24000))),2);return samples;}
static void pack(const std::filesystem::path& path,int frequency){
    Bytes meta,recs;string(meta,"fixture");string(meta,"en");string(meta,"arpabet");number(meta,24000,4);
    number(recs,1,4);string(recs,"record-one");string(recs,"hello world");number(recs,2,4);
    for(int i=0;i<2;++i){string(recs,i?"world":"hello");number(recs,i*2400,8);number(recs,(i+1)*2400,8);number(recs,std::bit_cast<std::uint32_t>(1.f),4);}
    number(recs,2,4);
    for(int i=0;i<2;++i){string(recs,i?"W":"HH");number(recs,i,4);number(recs,i*2400,8);number(recs,(i+1)*2400,8);number(recs,std::bit_cast<std::uint32_t>(1.f),4);}
    std::vector<std::pair<std::string,Bytes>> sections{{"META",meta},{"RECS",recs},{"AUDI",audio(frequency)},{"XTRA",{1,2,3}}};
    std::uint64_t total=24+32*sections.size();for(const auto& section:sections)total+=section.second.size();
    Bytes out;raw(out,std::string_view("VCPACK\0\0",8));number(out,1,4);number(out,sections.size(),4);number(out,total,8);
    std::uint64_t offset=24+32*sections.size();
    for(const auto& [name,bytes]:sections){raw(out,name);number(out,name=="XTRA"?0:1,4);number(out,offset,8);number(out,bytes.size(),8);number(out,crc(bytes),4);number(out,0,4);offset+=bytes.size();}
    for(const auto& section:sections)out.insert(out.end(),section.second.begin(),section.second.end());write(path,out);
}
int main(){
    const auto root=std::filesystem::temp_directory_path()/std::filesystem::path(u8"vctts-phonomenal-żą-test");
    assert(!std::filesystem::exists(root));std::filesystem::create_directories(root);
    const auto voice=root/"voice.vcpack";pack(voice,220);tts_phonomenal::select(voice);
    assert(tts_phonomenal::enabled());auto first=tts_phonomenal::speak("hello world");
    if(first.empty())std::cerr<<tts_phonomenal::last_error()<<'\n';
    assert(first.size()>44 && std::string(first.begin(),first.begin()+4)=="RIFF");assert(tts_phonomenal::last_error().empty());
    auto modified=std::filesystem::last_write_time(voice);pack(voice,440);std::filesystem::last_write_time(voice,modified+std::chrono::seconds(2));
    auto second=tts_phonomenal::speak("hello world");assert(second.size()>44 && second!=first);
    auto pcm=audio(330);Bytes wav;raw(wav,"RIFF");number(wav,36+pcm.size(),4);raw(wav,"WAVEfmt ");number(wav,16,4);number(wav,1,2);number(wav,1,2);number(wav,24000,4);number(wav,48000,4);number(wav,2,2);number(wav,16,2);raw(wav,"data");number(wav,pcm.size(),4);wav.insert(wav.end(),pcm.begin(),pcm.end());write(root/"master.wav",wav);
    const auto legacy=root/"voice.phbank";
    {std::ofstream out(legacy);out<<"PHONOMENAL_BANK\t2\nmerc\tfixture\nsample_rate\t24000\nmaster_audio\tmaster.wav\nrecord\tfixture\nword\thello\t0\t2400\t1\nphoneme\tHH\t0\t0\t2400\t1\nword\tworld\t2400\t4800\t1\nphoneme\tW\t1\t2400\t4800\t1\nendrecord\n";}
    tts_phonomenal::select(legacy);auto migrated=tts_phonomenal::speak("hello world");if(migrated.empty())std::cerr<<tts_phonomenal::last_error()<<'\n';assert(migrated.size()>44);
    tts_phonomenal::select(root/"missing.vcpack");assert(tts_phonomenal::speak("hello").empty() && !tts_phonomenal::last_error().empty());
    tts_phonomenal::select(voice);assert(!tts_phonomenal::speak("hello").empty() && tts_phonomenal::last_error().empty());
    tts_phonomenal::disable();assert(!tts_phonomenal::enabled());std::filesystem::remove_all(root);
    std::cout<<"Current Phonomenal vcpack/phbank synthesis, Unicode paths, pack reload, explicit errors and recovery passed\n";
}
