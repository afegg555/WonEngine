#include "WindowWin32.h"

#if defined(_WIN32)
namespace won::platform
{
    namespace
    {
        constexpr const char* k_window_class_name = "WonEngineWindowClass";

        LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
        {
            switch (message)
            {
            case WM_DESTROY:
                PostQuitMessage(0);
                return 0;
            default:
                return DefWindowProc(hwnd, message, wparam, lparam);
            }
        }

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
            wc.lpfnWndProc = WindowProc;
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
        if (!desc.resizable)
        {
            style &= ~WS_THICKFRAME;
            style &= ~WS_MAXIMIZEBOX;
        }

        RECT rect = { 0, 0, desc.width, desc.height };
        AdjustWindowRect(&rect, style, FALSE);

        hwnd = CreateWindowExA(0, k_window_class_name, desc.title, style, CW_USEDEFAULT, CW_USEDEFAULT,
            rect.right - rect.left, rect.bottom - rect.top, nullptr, nullptr, GetModuleHandleA(nullptr), nullptr);

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
        if (hwnd)
        {
            RECT rect = { 0, 0, new_width, new_height };
            AdjustWindowRect(&rect, GetWindowLongA(hwnd, GWL_STYLE), FALSE);
            SetWindowPos(hwnd, nullptr, 0, 0, rect.right - rect.left, rect.bottom - rect.top, SWP_NOMOVE | SWP_NOZORDER);
        }
        width = new_width;
        height = new_height;
    }

    int WindowWin32::GetWidth() const
    {
        return width;
    }

    int WindowWin32::GetHeight() const
    {
        return height;
    }
}
#endif
