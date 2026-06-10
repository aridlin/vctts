#include "driver_setup.h"

#include <cstdlib>
#include <iostream>

static void expect(bool condition, const char* message)
{
    if (!condition) {
        std::cerr << "Test failed: " << message << std::endl;
        std::exit(1);
    }
}

static void test_parse_manifest()
{
    driver_setup::Manifest manifest;
    bool ok = driver_setup::parse_manifest_text(
        L"# vctts test\n"
        L"version = 25.7.14\n"
        L"inf = VirtualAudioDriver.inf\n"
        L"playback_matches = Virtual Speaker | Cable Input\n"
        L"capture_matches = Virtual Microphone | Cable Output\n",
        manifest);

    expect(ok, "manifest should parse");
    expect(manifest.version == L"25.7.14", "version should parse");
    expect(manifest.infPath == L"VirtualAudioDriver.inf", "inf should parse");
    expect(manifest.playbackMatches.size() == 2, "playback matches should parse");
    expect(manifest.captureMatches.size() == 2, "capture matches should parse");
}

static void test_find_matching_device()
{
    std::vector<AudioDevice> devices{
        { L"", L"Realtek Speakers" },
        { L"", L"Virtual Microphone Array" },
    };
    int found = driver_setup::find_matching_device(devices, { L"virtual microphone" });
    expect(found == 1, "matching should be case-insensitive");
}

int main()
{
    test_parse_manifest();
    test_find_matching_device();
    std::cout << "All driver setup tests passed!" << std::endl;
    return 0;
}
