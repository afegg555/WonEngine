#include "SplashWindowWin32.h"

#if defined(_WIN32)
#include <gdiplus.h>
#include <windowsx.h>

namespace won::platform
{
    namespace
    {
        constexpr const char* splash_window_class_name = "WonEngineSplashWindowClass";

        void RegisterSplashWindowClass()
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }

            WNDCLASSEXA wc = {};
            wc.cbSize = sizeof(WNDCLASSEXA);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = SplashWindowWin32::WindowProc;
            wc.hInstance = GetModuleHandleA(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = splash_window_class_name;
            RegisterClassExA(&wc);
            registered = true;
        }
    }

    SplashWindowWin32::SplashWindowWin32(const SplashWindowDesc& desc) : title(desc.title ? desc.title : ""), status(desc.status ? desc.status : ""), image_path(desc.image_path ? desc.image_path : ""), font_name(desc.style.font_name ? desc.style.font_name : ""), style(desc.style), width(desc.width), height(desc.height)
    {
        RegisterSplashWindowClass();

        if (!image_path.empty())
        {
            Gdiplus::GdiplusStartupInput startup_input = {};
            if (Gdiplus::GdiplusStartup(&gdiplus_token, &startup_input, nullptr) == Gdiplus::Ok)
            {
                const int wide_length = MultiByteToWideChar(CP_UTF8, 0, image_path.c_str(), -1, nullptr, 0);
                if (wide_length > 0)
                {
                    std::wstring wide_image_path(static_cast<Size>(wide_length - 1), L'\0');
                    MultiByteToWideChar(CP_UTF8, 0, image_path.c_str(), -1, wide_image_path.data(), wide_length);
                    image.reset(Gdiplus::Bitmap::FromFile(wide_image_path.c_str(), FALSE));
                    if (!image || image->GetLastStatus() != Gdiplus::Ok)
                    {
                        image.reset();
                    }
                    else
                    {
                        width = static_cast<int>(image->GetWidth());
                        height = static_cast<int>(image->GetHeight());
                    }
                }
            }
        }

        const int screen_width = GetSystemMetrics(SM_CXSCREEN);
        const int screen_height = GetSystemMetrics(SM_CYSCREEN);
        const int window_x = (screen_width - width) / 2;
        const int window_y = (screen_height - height) / 2;
        hwnd = CreateWindowExA(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, splash_window_class_name, title.c_str(), WS_POPUP,
            window_x, window_y, width, height, nullptr, nullptr, GetModuleHandleA(nullptr), this);

        if (desc.visible)
        {
            Show();
        }
    }

    SplashWindowWin32::~SplashWindowWin32()
    {
        Close();
        image.reset();
        if (gdiplus_token != 0)
        {
            Gdiplus::GdiplusShutdown(gdiplus_token);
            gdiplus_token = 0;
        }
    }

    void* SplashWindowWin32::GetNativeHandle() const
    {
        return hwnd;
    }

    void SplashWindowWin32::Show()
    {
        if (hwnd)
        {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }
    }

    void SplashWindowWin32::Close()
    {
        if (hwnd)
        {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    void SplashWindowWin32::SetStatus(const char* new_status)
    {
        status = new_status ? new_status : "";
        if (hwnd)
        {
            InvalidateRect(hwnd, nullptr, TRUE);
            UpdateWindow(hwnd);
        }
    }

    void SplashWindowWin32::Paint()
    {
        if (!hwnd)
        {
            return;
        }

        PAINTSTRUCT paint = {};
        HDC hdc = BeginPaint(hwnd, &paint);
        RECT rect = { 0, 0, width, height };
        HBRUSH background = CreateSolidBrush(RGB(style.background_color.r, style.background_color.g, style.background_color.b));
        FillRect(hdc, &rect, background);
        DeleteObject(background);

        if (image)
        {
            Gdiplus::Graphics graphics(hdc);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(image.get(), 0, 0, width, height);
        }

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(style.title_color.r, style.title_color.g, style.title_color.b));

        HFONT title_font = CreateFontA(-style.title_font_height, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, font_name.c_str());
        HFONT old_font = static_cast<HFONT>(SelectObject(hdc, title_font));
        RECT title_rect = { style.horizontal_padding, style.title_top, width - style.horizontal_padding, style.title_top + style.title_height };
        DrawTextA(hdc, title.c_str(), -1, &title_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SetTextColor(hdc, RGB(style.status_color.r, style.status_color.g, style.status_color.b));
        HFONT status_font = CreateFontA(-style.status_font_height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, font_name.c_str());
        SelectObject(hdc, status_font);
        RECT status_rect = { style.horizontal_padding, style.status_top, width - style.horizontal_padding, style.status_top + style.status_height };
        DrawTextA(hdc, status.c_str(), -1, &status_rect, DT_CENTER | DT_SINGLELINE | DT_VCENTER);

        SelectObject(hdc, old_font);
        DeleteObject(status_font);
        DeleteObject(title_font);
        EndPaint(hwnd, &paint);
    }

    LRESULT CALLBACK SplashWindowWin32::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            auto* create_struct = reinterpret_cast<CREATESTRUCTA*>(lparam);
            auto* window = static_cast<SplashWindowWin32*>(create_struct->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }

        auto* window = reinterpret_cast<SplashWindowWin32*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));
        switch (message)
        {
        case WM_PAINT:
            if (window)
            {
                window->Paint();
                return 0;
            }
            break;
        case WM_CLOSE:
            DestroyWindow(hwnd);
            return 0;
        case WM_NCDESTROY:
            if (window && window->hwnd == hwnd)
            {
                window->hwnd = nullptr;
            }
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, 0);
            break;
        default:
            break;
        }

        return DefWindowProcA(hwnd, message, wparam, lparam);
    }
}
#endif
