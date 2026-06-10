#include "overlay_window.h"

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>

#include <algorithm>
#include <string>

using namespace Gdiplus;

namespace
{
    constexpr const wchar_t* kClassName = L"VCTTS_CLICKTHROUGH_OVERLAY";

    HWND g_hwnd = nullptr;
    ULONG_PTR g_gdiplusToken = 0;
    bool g_registered = false;

    LRESULT CALLBACK overlay_wndproc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        switch (msg)
        {
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_ERASEBKGND:
            return 1;
        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
        }
    }

    void add_rounded_rect(GraphicsPath& path, RectF r, float radius)
    {
        const float d = radius * 2.0f;
        path.AddArc(r.X, r.Y, d, d, 180.0f, 90.0f);
        path.AddArc(r.X + r.Width - d, r.Y, d, d, 270.0f, 90.0f);
        path.AddArc(r.X + r.Width - d, r.Y + r.Height - d, d, d, 0.0f, 90.0f);
        path.AddArc(r.X, r.Y + r.Height - d, d, d, 90.0f, 90.0f);
        path.CloseFigure();
    }

    void draw_string(Graphics& g, const std::wstring& text, const RectF& rect, float size,
                     Color color, int style = FontStyleRegular,
                     StringAlignment align = StringAlignmentNear,
                     StringAlignment lineAlign = StringAlignmentCenter)
    {
        FontFamily family(L"Segoe UI");
        Font font(&family, size, style, UnitPixel);
        SolidBrush brush(color);
        StringFormat fmt;
        fmt.SetAlignment(align);
        fmt.SetLineAlignment(lineAlign);
        fmt.SetTrimming(StringTrimmingEllipsisWord);
        fmt.SetFormatFlags(StringFormatFlagsLineLimit);
        g.DrawString(text.c_str(), (INT)text.size(), &font, rect, &fmt, &brush);
    }

    UINT target_dpi(HWND target)
    {
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32 && target && IsWindow(target)) {
            using GetDpiForWindowFn = UINT (WINAPI*)(HWND);
            auto getDpiForWindow = reinterpret_cast<GetDpiForWindowFn>(
                GetProcAddress(user32, "GetDpiForWindow"));
            if (getDpiForWindow)
                return getDpiForWindow(target);
        }
        if (user32) {
            using GetDpiForSystemFn = UINT (WINAPI*)();
            auto getDpiForSystem = reinterpret_cast<GetDpiForSystemFn>(
                GetProcAddress(user32, "GetDpiForSystem"));
            if (getDpiForSystem)
                return getDpiForSystem();
        }

        HDC screen = GetDC(nullptr);
        UINT dpi = screen ? (UINT)GetDeviceCaps(screen, LOGPIXELSX) : 96;
        if (screen) ReleaseDC(nullptr, screen);
        return dpi ? dpi : 96;
    }

    HWND target_window(const AppState& state)
    {
        if (state.prevForeground && IsWindow(state.prevForeground))
            return state.prevForeground;

        HWND fg = GetForegroundWindow();
        if (fg == g_hwnd || fg == state.hwnd)
            return state.prevForeground && IsWindow(state.prevForeground) ? state.prevForeground : nullptr;
        return fg;
    }

    RECT monitor_rect_for(HWND hwnd)
    {
        HMONITOR mon = MonitorFromWindow(hwnd ? hwnd : GetForegroundWindow(), MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi{ sizeof(mi) };
        GetMonitorInfoW(mon, &mi);
        return mi.rcWork;
    }

    void overlay_bounds(const AppState& state, POINT& dst, SIZE& size)
    {
        HWND target = target_window(state);
        RECT work = monitor_rect_for(target);

        const float scale = std::max(1.0f, target_dpi(target) / 96.0f);
        const int margin = (int)(32.0f * scale);
        const int wantedW = (int)(720.0f * scale);
        const int wantedH = (int)(128.0f * scale);
        const int workW = (int)(work.right - work.left);
        const int maxW = std::max(260, workW - margin * 2);

        size.cx = std::min(wantedW, maxW);
        size.cy = wantedH;
        dst.x = work.left + ((work.right - work.left) - size.cx) / 2;
        dst.y = work.top + (int)(40.0f * scale);
    }

    void render_overlay(AppState& state, POINT dst, SIZE size)
    {
        HDC screen = GetDC(nullptr);
        HDC mem = CreateCompatibleDC(screen);
        if (!screen || !mem) {
            if (mem) DeleteDC(mem);
            if (screen) ReleaseDC(nullptr, screen);
            return;
        }

        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = size.cx;
        bmi.bmiHeader.biHeight = -size.cy;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void* bits = nullptr;
        HBITMAP bmp = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!bmp) {
            DeleteDC(mem);
            ReleaseDC(nullptr, screen);
            return;
        }
        HGDIOBJ old = SelectObject(mem, bmp);

        Graphics g(mem);
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
        g.Clear(Color(0, 0, 0, 0));

        const float scale = std::max(1.0f, size.cx / 720.0f);
        g.ScaleTransform(scale, scale);
        const float w = size.cx / scale;
        const float h = size.cy / scale;

        RectF panel(0.5f, 0.5f, w - 1.0f, h - 1.0f);
        GraphicsPath panelPath;
        add_rounded_rect(panelPath, panel, 18.0f);
        SolidBrush panelBrush(Color(218, 17, 19, 24));
        Pen panelPen(Color(190, 86, 99, 124), 1.0f);
        g.FillPath(&panelBrush, &panelPath);
        g.DrawPath(&panelPen, &panelPath);

        RectF pill(20.0f, 18.0f, 94.0f, 24.0f);
        GraphicsPath pillPath;
        add_rounded_rect(pillPath, pill, 12.0f);
        SolidBrush pillBrush(Color(235, 84, 117, 255));
        g.FillPath(&pillBrush, &pillPath);
        draw_string(g, L"VOICE TTS", pill, 12.0f, Color(255, 255, 255, 255),
                    FontStyleBold, StringAlignmentCenter, StringAlignmentCenter);

        draw_string(g, L"Recording", RectF(128.0f, 18.0f, 160.0f, 24.0f),
                    14.0f, Color(255, 164, 253, 204), FontStyleBold);
        draw_string(g, L"Enter speak / Esc cancel", RectF(w - 220.0f, 18.0f, 196.0f, 24.0f),
                    13.0f, Color(220, 198, 207, 221), FontStyleRegular, StringAlignmentFar);

        std::wstring body = state.copyBuffer();
        const bool empty = body.empty();
        if (empty) body = L"Start typing...";

        RectF preview(20.0f, 54.0f, w - 40.0f, 52.0f);
        draw_string(g, body, preview, 22.0f,
                    empty ? Color(165, 198, 207, 221) : Color(255, 247, 250, 255),
                    FontStyleRegular, StringAlignmentNear, StringAlignmentNear);

        POINT src{ 0, 0 };
        BLENDFUNCTION blend{ AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
        UpdateLayeredWindow(g_hwnd, screen, &dst, &size, mem, &src, 0, &blend, ULW_ALPHA);

        SelectObject(mem, old);
        DeleteObject(bmp);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
    }
}

namespace overlay_window
{
    bool create(AppState&, HINSTANCE hInstance)
    {
        GdiplusStartupInput input{};
        if (GdiplusStartup(&g_gdiplusToken, &input, nullptr) != Ok)
            return false;

        WNDCLASSEXW wc{};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = overlay_wndproc;
        wc.hInstance = hInstance;
        wc.lpszClassName = kClassName;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);

        if (!RegisterClassExW(&wc)) {
            GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            return false;
        }
        g_registered = true;

        g_hwnd = CreateWindowExW(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW,
            kClassName,
            L"VCTTS Overlay",
            WS_POPUP,
            0, 0, 1, 1,
            nullptr, nullptr, hInstance, nullptr
        );

        if (!g_hwnd) {
            UnregisterClassW(kClassName, hInstance);
            g_registered = false;
            GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            return false;
        }

        return true;
    }

    void destroy(HINSTANCE hInstance)
    {
        if (g_hwnd) {
            DestroyWindow(g_hwnd);
            g_hwnd = nullptr;
        }
        if (g_registered) {
            UnregisterClassW(kClassName, hInstance);
            g_registered = false;
        }
        if (g_gdiplusToken) {
            GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
    }

    void tick(AppState& state)
    {
        if (!g_hwnd) return;
        if (!state.recording.load()) {
            hide();
            return;
        }

        POINT dst{};
        SIZE size{};
        overlay_bounds(state, dst, size);
        render_overlay(state, dst, size);
        SetWindowPos(g_hwnd, HWND_TOPMOST, dst.x, dst.y, size.cx, size.cy,
                     SWP_NOACTIVATE | SWP_SHOWWINDOW);
        ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
    }

    void hide()
    {
        if (g_hwnd && IsWindowVisible(g_hwnd))
            ShowWindow(g_hwnd, SW_HIDE);
    }
}
