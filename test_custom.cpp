#include "custom_tts.h"
#include <iostream>
#include <string>

int main()
{
    std::cout << "Starting custom TTS test...\n";

    // Test with a dummy command that just echo some data
    // We use a Windows specific cmd.exe command to echo text
    std::wstring text = L"hello world";
    const char* cmd = "cmd.exe /c echo {text}";

    auto data = custom_tts::SpeakCustomCommand(text, cmd);

    if (data.empty())
    {
        std::cerr << "FAIL: SpeakCustomCommand returned empty data\n";
        return 1;
    }

    std::cout << "SUCCESS: SpeakCustomCommand returned " << data.size() << " bytes.\n";

    // Since we echo'd 'hello world', the data should contain it.
    std::string dataStr(data.begin(), data.end());
    if (dataStr.find("hello world") == std::string::npos)
    {
        std::cerr << "FAIL: The returned output didn't contain the expected text. Output was:\n" << dataStr << "\n";
        return 1;
    }

    std::cout << "SUCCESS: The output contained the expected text.\n";
    return 0;
}
