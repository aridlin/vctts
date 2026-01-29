#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <tchar.h>

#include <chrono>
#include <string>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstdio>

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

// ----------------------------- Globals -----------------------------
static HWND g_hwnd = nullptr;
static HHOOK g_kbHook = nullptr;

static ID3D11Device*            g_pd3dDevice = nullptr;
static ID3D11DeviceContext*     g_pd3dDeviceContext = nullptr;
static IDXGISwapChain*          g_pSwapChain = nullptr;
static ID3D11RenderTargetView*  g_mainRenderTargetView = nullptr;

static std::atomic<bool> g_recording{false};
static std::atomic<bool> g_exitRequested{false};

static std::mutex g_bufMutex;
static std::wstring g_buffer;

static std::chrono::steady_clock::time_point g_lastInput = std::chrono::steady_clock::now();
static constexpr int TIMEOUT_SECONDS = 30;

static HWND g_prevForeground = nullptr;

// Modifier tracking (hook-driven)
static std::atomic<bool> g_shift{false};
static std::atomic<bool> g_ctrl{false};
static std::atomic<bool> g_alt{false};

static std::chrono::steady_clock::time_point g_lastToggle = std::chrono::steady_clock::now();
static constexpr double TOGGLE_DEBOUNCE_SEC = 0.35;

// ----------------------------- Helpers -----------------------------
static void CreateRenderTarget() {
    ID3D11Texture2D* pBackBuffer = nullptr;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    if (pBackBuffer) {
        g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
        pBackBuffer->Release();
    }
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static bool CreateDeviceD3D(HWND hWnd) {
    DXGI_SWAP_CHAIN_DESC sd{};
    sd.BufferCount = 2;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
#ifdef _DEBUG
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0 };

    HRESULT hr = D3D11CreateDeviceAndSwapChain(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr,
        createDeviceFlags, featureLevelArray, 2,
        D3D11_SDK_VERSION, &sd, &g_pSwapChain,
        &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext
    );

    if (FAILED(hr)) return false;
    CreateRenderTarget();
    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static void StartRecording() {
    if (g_recording.load()) return;

    g_prevForeground = GetForegroundWindow();
    {
        std::lock_guard<std::mutex> lock(g_bufMutex);
        g_buffer.clear();
    }

    g_recording.store(true);
    g_lastInput = std::chrono::steady_clock::now();

    ShowWindow(g_hwnd, SW_SHOW);
    SetWindowPos(g_hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);

    // You said it's OK to steal focus now:
    SetForegroundWindow(g_hwnd);
    SetActiveWindow(g_hwnd);
}

static void StopRecording() {
    if (!g_recording.load()) return;
    g_recording.store(false);

    ShowWindow(g_hwnd, SW_HIDE);

    // Focus back to previous app/game
    if (g_prevForeground) {
        SetForegroundWindow(g_prevForeground);
        SetActiveWindow(g_prevForeground);
    }

    // (Next step later: run SAPI TTS on g_buffer and play)
}

static void ToggleRecording() {
    auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - g_lastToggle).count();
    if (dt < TOGGLE_DEBOUNCE_SEC) return;
    g_lastToggle = now;

    if (g_recording.load()) StopRecording();
    else StartRecording();
}

// Converts a key event into actual Unicode chars using ToUnicodeEx.
// This works best with real keyboard layouts and will naturally produce Polish chars if layout does.
static void AppendKeyAsText(DWORD vkCode, DWORD scanCode) {
    HKL layout = GetKeyboardLayout(0);

    BYTE ks[256]{};
    if (g_shift.load()) ks[VK_SHIFT] = 0x80;
    if (g_ctrl.load())  ks[VK_CONTROL] = 0x80;
    if (g_alt.load())   ks[VK_MENU] = 0x80;
    // CapsLock state from system:
    if ((GetKeyState(VK_CAPITAL) & 1) != 0) ks[VK_CAPITAL] = 0x01;

    wchar_t out[8]{};
    int rc = ToUnicodeEx((UINT)vkCode, (UINT)scanCode, ks, out, 8, 0, layout);
    if (rc > 0) {
        std::lock_guard<std::mutex> lock(g_bufMutex);
        g_buffer.append(out, out + rc);
        g_lastInput = std::chrono::steady_clock::now();
    }
}

static void Backspace() {
    std::lock_guard<std::mutex> lock(g_bufMutex);
    if (!g_buffer.empty()) g_buffer.pop_back();
    g_lastInput = std::chrono::steady_clock::now();
}

static std::string SanitizePreview(const std::wstring& ws) {
    // You said it's fine to show placeholders like %a etc.
    // For now: ASCII pass-through; known Polish chars -> %x; unknown -> '?'
    std::string out;
    out.reserve(ws.size());

    auto emit = [&](const char* s){ out += s; };

    for (wchar_t c : ws) {
        if (c >= 32 && c < 127) { out.push_back((char)c); continue; }

        switch (c) {
            case L'ą': emit("%a"); break;
            case L'ć': emit("%c"); break;
            case L'ę': emit("%e"); break;
            case L'ł': emit("%l"); break;
            case L'ń': emit("%n"); break;
            case L'ó': emit("%o"); break;
            case L'ś': emit("%s"); break;
            case L'ż': emit("%z"); break;
            case L'ź': emit("%x"); break;
            case L'Ą': emit("%A"); break;
            case L'Ć': emit("%C"); break;
            case L'Ę': emit("%E"); break;
            case L'Ł': emit("%L"); break;
            case L'Ń': emit("%N"); break;
            case L'Ó': emit("%O"); break;
            case L'Ś': emit("%S"); break;
            case L'Ż': emit("%Z"); break;
            case L'Ź': emit("%X"); break;
            default: out.push_back('?'); break;
        }
    }
    return out;
}

// ----------------------------- Keyboard Hook -----------------------------
static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION) return CallNextHookEx(g_kbHook, nCode, wParam, lParam);

    const KBDLLHOOKSTRUCT* k = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);
    const bool down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
    const bool up   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);

    // Update modifier state
    if (k->vkCode == VK_LSHIFT || k->vkCode == VK_RSHIFT || k->vkCode == VK_SHIFT) {
        if (down) g_shift.store(true);
        if (up)   g_shift.store(false);
    }
    if (k->vkCode == VK_LCONTROL || k->vkCode == VK_RCONTROL || k->vkCode == VK_CONTROL) {
        if (down) g_ctrl.store(true);
        if (up)   g_ctrl.store(false);
    }
    if (k->vkCode == VK_LMENU || k->vkCode == VK_RMENU || k->vkCode == VK_MENU) {
        if (down) g_alt.store(true);
        if (up)   g_alt.store(false);
    }

    // Exit hotkey example: Ctrl+Shift+Tab+E (your old combo)
    // We check on E-down while Ctrl+Shift held and Tab currently down.
    if (down && k->vkCode == 'E') {
        if (g_ctrl.load() && g_shift.load() && (GetAsyncKeyState(VK_TAB) & 0x8000)) {
            g_exitRequested.store(true);
            return 1; // swallow
        }
    }

    // Toggle hotkey: Ctrl+Backspace
    if (down && k->vkCode == VK_BACK) {
        if (g_ctrl.load()) {
            ToggleRecording();
            return 1; // swallow the toggle key combo
        }
    }

    // While recording, build buffer & swallow keys (optional but good for games)
    if (g_recording.load()) {
        if (down) {
            if (k->vkCode == VK_RETURN) {
                StopRecording();
                return 1;
            }
            if (k->vkCode == VK_BACK) {
                Backspace();
                return 1;
            }
            if (k->vkCode == VK_SPACE) {
                std::lock_guard<std::mutex> lock(g_bufMutex);
                g_buffer.push_back(L' ');
                g_lastInput = std::chrono::steady_clock::now();
                return 1;
            }

            // Ignore pure modifiers
            if (k->vkCode == VK_SHIFT || k->vkCode == VK_CONTROL || k->vkCode == VK_MENU) {
                return 1;
            }

            // Try convert key to text
            AppendKeyAsText(k->vkCode, k->scanCode);
            return 1; // swallow so the game doesn't react
        }
        return 1; // swallow key-up too
    }

    return CallNextHookEx(g_kbHook, nCode, wParam, lParam);
}

// ----------------------------- Win32 Proc -----------------------------
extern LRESULT ImGui_ImplWin32_WndProcHandler(HWND, UINT, WPARAM, LPARAM);

static LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT menu
            return 0;
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hWnd, msg, wParam, lParam);
}

// ----------------------------- Entry -----------------------------
int APIENTRY wWinMain(HINSTANCE hInstance, HINSTANCE, LPWSTR, int) {
    // Window class
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_CLASSDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"TTS_OVERLAY_IMGUI";
    RegisterClassExW(&wc);

    // Create hidden window (we show it only when recording)
    g_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        wc.lpszClassName,
        L"Voice Typing (ImGui)",
        WS_OVERLAPPEDWINDOW,
        560, 320, 720, 360,
        nullptr, nullptr, wc.hInstance, nullptr
    );

    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    ShowWindow(g_hwnd, SW_HIDE);
    UpdateWindow(g_hwnd);

    // ImGui setup
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui::StyleColorsDark();

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Global keyboard hook
    g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandleW(nullptr), 0);
    if (!g_kbHook) {
        MessageBoxW(nullptr, L"Failed to install keyboard hook.", L"Error", MB_ICONERROR);
        g_exitRequested.store(true);
    }

    // Message loop
    MSG msg{};
    while (!g_exitRequested.load()) {
        while (PeekMessageW(&msg, nullptr, 0U, 0U, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) g_exitRequested.store(true);
        }
        if (g_exitRequested.load()) break;

        // Timeout
        if (g_recording.load()) {
            auto now = std::chrono::steady_clock::now();
            auto dt = std::chrono::duration_cast<std::chrono::seconds>(now - g_lastInput).count();
            if (dt >= TIMEOUT_SECONDS) StopRecording();
        }

        // Render only when window is shown (recording)
        if (!IsWindowVisible(g_hwnd)) {
            Sleep(10);
            continue;
        }

        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // UI
        ImGui::SetNextWindowSize(ImVec2(700, 320), ImGuiCond_Always);
        ImGui::Begin("Voice Typing", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        ImGui::TextDisabled("Recording: %s  |  Toggle: Ctrl+Backspace  |  Stop: Enter  |  Exit: Ctrl+Shift+Tab+E",
                            g_recording.load() ? "YES" : "no");

        std::wstring copy;
        {
            std::lock_guard<std::mutex> lock(g_bufMutex);
            copy = g_buffer;
        }
        std::string preview = SanitizePreview(copy);

        ImGui::Separator();
        ImGui::TextUnformatted("Preview:");
        ImGui::BeginChild("##preview", ImVec2(0, 220), true);
        ImGui::TextUnformatted(preview.c_str());
        ImGui::EndChild();

        if (ImGui::Button("Stop & (later) Speak")) {
            StopRecording();
        }
        ImGui::SameLine();
        if (ImGui::Button("Exit")) {
            g_exitRequested.store(true);
        }

        ImGui::End();

        ImGui::Render();
        const float clear_color_with_alpha[4] = { 0.08f, 0.08f, 0.09f, 1.0f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
        g_pSwapChain->Present(1, 0);
    }

    // Cleanup
    if (g_kbHook) UnhookWindowsHookEx(g_kbHook);
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    UnregisterClassW(wc.lpszClassName, wc.hInstance);
    return 0;
}

