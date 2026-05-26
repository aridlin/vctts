#pragma once

#include "app_state.h"

#include <windows.h>

#include <string>
#include <vector>

namespace Gdiplus
{
    class Graphics;
}

enum class UiAction
{
    None,
    Quit,
    StartFromConfig,
    StopRecording,
    TestTts,
    TestTone,
    RefreshDevices,
    OpenConfig
};

class NativeUi
{
public:
    struct Rect
    {
        float x = 0;
        float y = 0;
        float w = 0;
        float h = 0;
    };

    bool init(HWND hwnd, AppState& state);
    void shutdown();

    UiAction tick();
    bool handle_message(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam, LRESULT& outResult);

private:
    enum class Hit : int
    {
        None = 0,
        Quit,
        Refresh,
        DeviceA,
        DeviceB,
        Keyless,
        TranslatorMode,
        TargetLanguage,
        IncomingMode,
        IncomingApp,
        MuteIncoming,
        Voice,
        CustomCommand,
        Start,
        TestTone,
        TestTts,
        StopSpeak,
        Config,
        DropdownItemBase = 1000
    };

    struct DropdownState
    {
        Hit owner = Hit::None;
        Rect rect{};
        int itemCount = 0;
        int selected = 0;
        bool open = false;
    };

    HWND hwnd_ = nullptr;
    AppState* state_ = nullptr;

    ULONG_PTR gdiplusToken_ = 0;
    UiAction pendingAction_ = UiAction::None;
    bool showingConfig_ = true;
    bool trackingMouse_ = false;
    bool mouseDown_ = false;
    bool customInputFocused_ = false;
    bool targetInputFocused_ = false;
    bool incomingAppInputFocused_ = false;
    float dpiScale_ = 1.0f;
    POINT mouse_{ -10000, -10000 };
    Hit active_ = Hit::None;
    Hit hot_ = Hit::None;
    DropdownState dropdown_{};

    int deviceSignature_ = -1;
    int voiceSignature_ = -1;
    std::wstring customCommandWide_;
    std::wstring targetLangWide_;
    std::wstring incomingAppWide_;

    int dropdown_visible_start() const;
    bool is_clickable(Hit hit) const;
    void refresh_dpi();
    void sync_model();
    void queue_action(UiAction action);
    void reset_interaction();
    void invalidate();

    void paint(HDC hdc);
    void paint_config(Gdiplus::Graphics& g, const Rect& bounds);
    void paint_recording(Gdiplus::Graphics& g, const Rect& bounds);

    Hit hit_test(float x, float y) const;
    void activate_hit(Hit hit);
    void open_dropdown(Hit owner, const Rect& rect, int count, int selected);
    void close_dropdown();
    void commit_dropdown_item(int index);

    void handle_char(wchar_t ch);
    void handle_keydown(WPARAM key);

    std::vector<std::wstring> device_names() const;
    std::vector<std::wstring> voice_names() const;
};
