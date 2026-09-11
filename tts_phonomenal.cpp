#include "tts_phonomenal.h"
#include "phonomenal_splicer/splicer.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <stdexcept>
namespace tts_phonomenal {
namespace {
std::atomic<bool> active{false};
std::mutex selection_mutex,synthesis_mutex;
std::filesystem::path selected,cached_path;
std::filesystem::file_time_type cached_time{};
std::unique_ptr<phonomenal_splicer::VoiceBank> bank;
thread_local std::string error;
}
void select(const std::filesystem::path& path){
    {std::lock_guard lock(selection_mutex);selected=path;}
    active.store(!path.empty());
}
void disable(){active.store(false);}
bool enabled(){return active.load();}
std::filesystem::path selected_path(){std::lock_guard lock(selection_mutex);return selected;}
const std::string& last_error(){return error;}
std::vector<std::uint8_t> speak(const std::string& text){
    error.clear();
    try {
        const auto path=selected_path();
        if(path.empty())throw std::runtime_error("Select a Phonomenal .vcpack or .phbank voice first.");
        // Pack I/O and planning are only reached on the existing speech worker.
        // Changing the selected voice never waits for an in-progress synthesis.
        std::lock_guard lock(synthesis_mutex);
        auto modified=std::filesystem::last_write_time(path);
        if(!bank || cached_path!=path || cached_time!=modified){
            auto loaded=std::make_unique<phonomenal_splicer::VoiceBank>(phonomenal_splicer::VoiceBank::Load(path));
            bank=std::move(loaded);cached_path=path;cached_time=modified;
        }
        auto plan=bank->PlanText(text);
        if(plan.units.empty())throw std::runtime_error("This voice pack could not cover the requested text.");
        auto wav=bank->SynthesizeWav(plan);
        if(wav.size()<=44)throw std::runtime_error("Phonomenal produced no audio for this text.");
        return wav;
    } catch(const std::exception& problem){error=problem.what();return {};}
}
}
