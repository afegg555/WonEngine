#include "WindowWin32.h"

#if defined(_WIN32)
#include <dwmapi.h>
#include <windowsx.h>

#ifdef IsMinimized
#undef IsMinimized
#endif
#ifdef IsMaximized
#undef IsMaximized
#endif

namespace won::platform
{
    namespace
    {
        constexpr const wchar_t* k_window_class_name = L"WonEngineWindowClass";

        std::wstring Utf8ToWide(const char* text)
        {
            if (!text || text[0] == '\0')
            {
                return {};
            }

            const int wide_length = MultiByteToWideChar(CP_UTF8, 0, text, -1, nullptr, 0);
            if (wide_length <= 0)
            {
                return {};
            }

            std::wstring wide_text(static_cast<Size>(wide_length - 1), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wide_text.data(), wide_length);
            return wide_text;
        }

        void RegisterWindowClass()
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }

            WNDCLASSEXW wc = {};
            wc.cbSize = sizeof(WNDCLASSEXW);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowWin32::WindowProc;
            wc.hInstance = GetModuleHandleW(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = k_window_class_name;
            RegisterClassExW(&wc);
            registered = true;
        }
    }

    WindowWin32::WindowWin32(const WindowDesc& desc) : width(desc.width), height(desc.height), use_title_bar(desc.use_title_bar), is_resizable(desc.resizable)
    {
        RegisterWindowClass();

        DWORD style = desc.use_title_bar ? WS_OVERLAPPEDWINDOW : WS_POPUP | WS_MINIMIZEBOX | WS_MAXIMIZEBOX;
        int window_x = CW_USEDEFAULT;
        int window_y = CW_USEDEFAULT;
        int window_width = desc.width;
        int window_height = desc.height;

        if (desc.fullscreen)
        {
            style = WS_POPUP;

            HMONITOR monitor = MonitorFromPoint(POINT { 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
            MONITORINFO monitor_info = {};
            monitor_info.cbSize = sizeof(MONITORINFO);
            if (GetMonitorInfoW(monitor, &monitor_info))
            {
                window_x = monitor_info.rcMonitor.left;
                window_y = monitor_info.rcMonitor.top;
                window_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
                window_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
            }
        }
        else if (desc.use_title_bar && !desc.resizable)
        {
            style &= ~WS_THICKFRAME;
            style &= ~WS_MAXIMIZEBOX;
        }

        RECT rect = { 0, 0, window_width, window_height };
        if (!desc.fullscreen && desc.use_title_bar)
        {
            AdjustWindowRect(&rect, style, FALSE);
            window_width = rect.right - rect.left;
            window_height = rect.bottom - rect.top;
        }

        const std::wstring title = Utf8ToWide(desc.title);
        hwnd = CreateWindowExW(0, k_window_class_name, title.c_str(), style, window_x, window_y,
            window_width, window_height, nullptr, nullptr, GetModuleHandleW(nullptr), this);

        if (hwnd && !desc.use_title_bar && !desc.fullscreen)
        {
            const DWM_WINDOW_CORNER_PREFERENCE corner_preference = DWMWCP_ROUND;
            DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner_preference, sizeof(corner_preference));
            HRGN rounded_region = CreateRoundRectRgn(0, 0, window_width + 1, window_height + 1, 14, 14);
            SetWindowRgn(hwnd, rounded_region, TRUE);
        }

        if (desc.fullscreen)
        {
            width = window_width;
            height = window_height;
        }

        if (desc.visible)
        {
            Show();
        }
        else
        {
            Hide();
        }
    }

    WindowWin32::~WindowWin32()
    {
        if (hwnd)
        {
            DestroyWindow(hwnd);
            hwnd = nullptr;
        }
    }

    void* WindowWin32::GetNativeHandle() const
    {
        return hwnd;
    }

    void WindowWin32::Show()
    {
        if (hwnd)
        {
            ShowWindow(hwnd, SW_SHOW);
            UpdateWindow(hwnd);
        }
    }

    void WindowWin32::Hide()
    {
        if (hwnd)
        {
            ShowWindow(hwnd, SW_HIDE);
        }
    }

    void WindowWin32::SetTitle(const char* title)
    {
        if (hwnd)
        {
            const std::wstring wide_title = Utf8ToWide(title);
            SetWindowTextW(hwnd, wide_title.c_str());
        }
    }

    void WindowWin32::Resize(int new_width, int new_height)
    {
        if (width == new_width &&
            height == new_height)
        {
            return;
        }
            
        if (hwnd)
        {
            RECT rect = { 0, 0, new_width, new_height };
            if (use_title_bar)
            {
                AdjustWindowRect(&rect, GetWindowLongW(hwnd, GWL_STYLE), FALSE);
            }
            SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        width = new_width;
        height = new_height;
        is_minimized = (new_width <= 0 || new_height <= 0);
        has_pending_resize = !is_minimized;
    }

    void WindowWin32::SetPosition(int x, int y)
    {
        if (!hwnd)
        {
            return;
        }

        SetWindowPos(hwnd, nullptr, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    bool WindowWin32::GetPosition(int& out_x, int& out_y) const
    {
        if (!hwnd)
        {
            return false;
        }

        RECT rect = {};
        if (!GetWindowRect(hwnd, &rect))
        {
            return false;
        }

        out_x = rect.left;
        out_y = rect.top;
        return true;
    }

    bool WindowWin32::GetCursorPosition(int& out_x, int& out_y) const
    {
        POINT cursor_position = {};
        if (!GetCursorPos(&cursor_position))
        {
            return false;
        }

        out_x = cursor_position.x;
        out_y = cursor_position.y;
        return true;
    }

    void WindowWin32::Minimize()
    {
        if (hwnd)
        {
            ShowWindow(hwnd, SW_MINIMIZE);
        }
    }

    void WindowWin32::Maximize()
    {
        if (hwnd)
        {
            ShowWindow(hwnd, SW_MAXIMIZE);
        }
    }

    void WindowWin32::Restore()
    {
        if (hwnd)
        {
            ShowWindow(hwnd, SW_RESTORE);
        }
    }

    void WindowWin32::Close()
    {
        if (hwnd)
        {
            PostMessageW(hwnd, WM_CLOSE, 0, 0);
        }
    }

    int WindowWin32::GetWidth() const
    {
        return width;
    }

    int WindowWin32::GetHeight() const
    {
        return height;
    }

    bool WindowWin32::IsFocused() const
    {
        if (!hwnd)
        {
            return false;
        }

        HWND focused_window = GetForegroundWindow();
        return focused_window == hwnd;
    }

    bool WindowWin32::IsMinimized() const
    {
        return is_minimized;
    }

    bool WindowWin32::IsMaximized() const
    {
        return hwnd && IsZoomed(hwnd);
    }

    bool WindowWin32::ConsumePendingResize()
    {
        const bool had_pending_resize = has_pending_resize;
        has_pending_resize = false;
        return had_pending_resize;
    }

    LRESULT CALLBACK WindowWin32::WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        if (message == WM_NCCREATE)
        {
            CREATESTRUCTW* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            auto* window = static_cast<WindowWin32*>(create_struct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }

        auto* window = reinterpret_cast<WindowWin32*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        switch (message)
        {
        case WM_GETMINMAXINFO:
            if (window && !window->use_title_bar)
            {
                auto* min_max_info = reinterpret_cast<MINMAXINFO*>(lparam);
                HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
                MONITORINFO monitor_info = {};
                monitor_info.cbSize = sizeof(MONITORINFO);
                if (GetMonitorInfoW(monitor, &monitor_info))
                {
                    const RECT& monitor_rect = monitor_info.rcMonitor;
                    const RECT& work_rect = monitor_info.rcWork;
                    min_max_info->ptMaxPosition.x = work_rect.left - monitor_rect.left;
                    min_max_info->ptMaxPosition.y = work_rect.top - monitor_rect.top;
                    min_max_info->ptMaxSize.x = work_rect.right - work_rect.left;
                    min_max_info->ptMaxSize.y = work_rect.bottom - work_rect.top;
                }
                return 0;
            }
            break;
        case WM_NCHITTEST:
            if (window && !window->use_title_bar)
            {
                const POINT mouse_position = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
                RECT window_rect = {};
                GetWindowRect(hwnd, &window_rect);

                const int border_size = window->is_resizable ? 8 : 0;
                const int x = mouse_position.x - window_rect.left;
                const int y = mouse_position.y - window_rect.top;
                const int window_width = window_rect.right - window_rect.left;
                const int window_height = window_rect.bottom - window_rect.top;

                if (window->is_resizable && !window->IsMaximized())
                {
                    const bool left = x < border_size;
                    const bool right = x >= window_width - border_size;
                    const bool top = y < border_size;
                    const bool bottom = y >= window_height - border_size;
                    if (left && top) return HTTOPLEFT;
                    if (right && top) return HTTOPRIGHT;
                    if (left && bottom) return HTBOTTOMLEFT;
                    if (right && bottom) return HTBOTTOMRIGHT;
                    if (left) return HTLEFT;
                    if (right) return HTRIGHT;
                    if (top) return HTTOP;
                    if (bottom) return HTBOTTOM;
                }
                return HTCLIENT;
            }
            break;
        }

        if (window && window->DispatchPlatformMessage(hwnd, message, static_cast<Size>(wparam), static_cast<Size>(lparam)))
        {
            return 1;
        }

        switch (message)
        {
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        case WM_SIZE:
            if (window)
            {
                window->width = static_cast<int>(LOWORD(lparam));
                window->height = static_cast<int>(HIWORD(lparam));
                window->is_minimized = wparam == SIZE_MINIMIZED || window->width <= 0 || window->height <= 0;
                window->has_pending_resize = !window->is_minimized;
                if (!window->use_title_bar && !window->is_minimized)
                {
                    if (wparam == SIZE_MAXIMIZED)
                    {
                        SetWindowRgn(hwnd, nullptr, TRUE);
                    }
                    else
                    {
                        HRGN rounded_region = CreateRoundRectRgn(0, 0, window->width + 1, window->height + 1, 14, 14);
                        SetWindowRgn(hwnd, rounded_region, TRUE);
                    }
                }
            }
            return 0;
        default:
            return DefWindowProcW(hwnd, message, wparam, lparam);
        }
    }
}
#endif
