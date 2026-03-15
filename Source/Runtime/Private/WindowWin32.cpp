#include "WindowWin32.h"

#if defined(_WIN32)
namespace won::platform
{
    namespace
    {
        constexpr const char* k_window_class_name = "WonEngineWindowClass";

        void RegisterWindowClass()
        {
            static bool registered = false;
            if (registered)
            {
                return;
            }

            WNDCLASSEXA wc = {};
            wc.cbSize = sizeof(WNDCLASSEXA);
            wc.style = CS_HREDRAW | CS_VREDRAW;
            wc.lpfnWndProc = WindowWin32::WindowProc;
            wc.hInstance = GetModuleHandleA(nullptr);
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.lpszClassName = k_window_class_name;
            RegisterClassExA(&wc);
            registered = true;
        }
    }

    WindowWin32::WindowWin32(const WindowDesc& desc) : width(desc.width), height(desc.height)
    {
        RegisterWindowClass();

        DWORD style = WS_OVERLAPPEDWINDOW;
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
            if (GetMonitorInfoA(monitor, &monitor_info))
            {
                window_x = monitor_info.rcMonitor.left;
                window_y = monitor_info.rcMonitor.top;
                window_width = monitor_info.rcMonitor.right - monitor_info.rcMonitor.left;
                window_height = monitor_info.rcMonitor.bottom - monitor_info.rcMonitor.top;
            }
        }
        else if (!desc.resizable)
        {
            style &= ~WS_THICKFRAME;
            style &= ~WS_MAXIMIZEBOX;
        }

        RECT rect = { 0, 0, window_width, window_height };
        if (!desc.fullscreen)
        {
            AdjustWindowRect(&rect, style, FALSE);
            window_width = rect.right - rect.left;
            window_height = rect.bottom - rect.top;
        }

        hwnd = CreateWindowExA(0, k_window_class_name, desc.title, style, window_x, window_y,
            window_width, window_height, nullptr, nullptr, GetModuleHandleA(nullptr), this);

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
            SetWindowTextA(hwnd, title ? title : "");
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
            AdjustWindowRect(&rect, GetWindowLongA(hwnd, GWL_STYLE), FALSE);
            SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        width = new_width;
        height = new_height;
        is_minimized = (new_width <= 0 || new_height <= 0);
        has_pending_resize = !is_minimized;
    }

    int WindowWin32::GetWidth() const
    {
        return width;
    }

    int WindowWin32::GetHeight() const
    {
        return height;
    }

    bool WindowWin32::IsMinimized() const
    {
        return is_minimized;
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
            CREATESTRUCTA* create_struct = reinterpret_cast<CREATESTRUCTA*>(lparam);
            auto* window = static_cast<WindowWin32*>(create_struct->lpCreateParams);
            SetWindowLongPtrA(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }

        auto* window = reinterpret_cast<WindowWin32*>(GetWindowLongPtrA(hwnd, GWLP_USERDATA));

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
                int width = static_cast<int>(LOWORD(lparam));
                int height = static_cast<int>(HIWORD(lparam));
                window->Resize(width, height);
            }
            return 0;
        default:
            return DefWindowProc(hwnd, message, wparam, lparam);
        }
    }
}
#endif
