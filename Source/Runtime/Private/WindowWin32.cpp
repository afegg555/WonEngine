#include "WindowWin32.h"
#include "Input.h"
#include "StringUtils.h"

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

        const WString title = desc.title ? utils::DecodeUtf8(desc.title) : WString();
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

    void WindowWin32::BringToForeground()
    {
        if (!hwnd)
        {
            return;
        }
        ShowWindow(hwnd, SW_SHOW);
        SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetFocus(hwnd);
    }

    void WindowWin32::SetTitle(const char* title)
    {
        if (hwnd)
        {
            const WString wide_title = title ? utils::DecodeUtf8(title) : WString();
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
        // wparam and lparam are message-specific

        if (message == WM_NCCREATE)
        {
            CREATESTRUCTW* create_struct = reinterpret_cast<CREATESTRUCTW*>(lparam);
            auto* window = static_cast<WindowWin32*>(create_struct->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        }

        auto* window = reinterpret_cast<WindowWin32*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));

        bool should_post_quit = false;
        bool should_update_size = false;
        bool message_handled = false;

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
					const bool right = x >= window_width - border_size;
					const bool bottom = y >= window_height - border_size;
					if (right && bottom) return HTBOTTOMRIGHT;
					if (right) return HTRIGHT;
					if (bottom) return HTBOTTOM;
				}
                return HTCLIENT;
            }
            break;
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP:
        {
            // For keyboard messages :
            // wparam: virtual-key code
            // lparam
            // bits 0-15   repeat count
            // bits 16 - 23  scan code
            // bit 24      extended key flag
            // bits 25 - 28  reserved
            // bit 29      context code
            // bit 30      previous key state
            // bit 31      transition state

            UINT virtual_key = static_cast<UINT>(wparam);
            if (virtual_key == VK_SHIFT)
            {
                const UINT scan_code = static_cast<UINT>((lparam >> 16) & 0xff);
                virtual_key = MapVirtualKeyW(scan_code, MAPVK_VSC_TO_VK_EX);
            }
            else if (virtual_key == VK_CONTROL)
            {
                virtual_key = (lparam & 0x01000000) ? VK_RCONTROL : VK_LCONTROL; // get extended key flag
            }
            else if (virtual_key == VK_MENU)
            {
                virtual_key = (lparam & 0x01000000) ? VK_RMENU : VK_LMENU;
            }

            io::Button button = io::BUTTON_NONE;
            if ((virtual_key >= 'A' && virtual_key <= 'Z') || (virtual_key >= '0' && virtual_key <= '9'))
            {
                button = static_cast<io::Button>(virtual_key);
            }
            else
            {
                switch (virtual_key)
                {
                case VK_UP: button = io::KEYBOARD_BUTTON_UP; break;
                case VK_DOWN: button = io::KEYBOARD_BUTTON_DOWN; break;
                case VK_LEFT: button = io::KEYBOARD_BUTTON_LEFT; break;
                case VK_RIGHT: button = io::KEYBOARD_BUTTON_RIGHT; break;
                case VK_SPACE: button = io::KEYBOARD_BUTTON_SPACE; break;
                case VK_RSHIFT: button = io::KEYBOARD_BUTTON_RSHIFT; break;
                case VK_LSHIFT: button = io::KEYBOARD_BUTTON_LSHIFT; break;
                case VK_F1: button = io::KEYBOARD_BUTTON_F1; break;
                case VK_F2: button = io::KEYBOARD_BUTTON_F2; break;
                case VK_F3: button = io::KEYBOARD_BUTTON_F3; break;
                case VK_F4: button = io::KEYBOARD_BUTTON_F4; break;
                case VK_F5: button = io::KEYBOARD_BUTTON_F5; break;
                case VK_F6: button = io::KEYBOARD_BUTTON_F6; break;
                case VK_F7: button = io::KEYBOARD_BUTTON_F7; break;
                case VK_F8: button = io::KEYBOARD_BUTTON_F8; break;
                case VK_F9: button = io::KEYBOARD_BUTTON_F9; break;
                case VK_F10: button = io::KEYBOARD_BUTTON_F10; break;
                case VK_F11: button = io::KEYBOARD_BUTTON_F11; break;
                case VK_F12: button = io::KEYBOARD_BUTTON_F12; break;
                case VK_RETURN: button = io::KEYBOARD_BUTTON_ENTER; break;
                case VK_ESCAPE: button = io::KEYBOARD_BUTTON_ESCAPE; break;
                case VK_HOME: button = io::KEYBOARD_BUTTON_HOME; break;
                case VK_RCONTROL: button = io::KEYBOARD_BUTTON_RCONTROL; break;
                case VK_LCONTROL: button = io::KEYBOARD_BUTTON_LCONTROL; break;
                case VK_DELETE: button = io::KEYBOARD_BUTTON_DELETE; break;
                case VK_BACK: button = io::KEYBOARD_BUTTON_BACKSPACE; break;
                case VK_NEXT: button = io::KEYBOARD_BUTTON_PAGEDOWN; break;
                case VK_PRIOR: button = io::KEYBOARD_BUTTON_PAGEUP; break;
                case VK_NUMPAD0: button = io::KEYBOARD_BUTTON_NUMPAD0; break;
                case VK_NUMPAD1: button = io::KEYBOARD_BUTTON_NUMPAD1; break;
                case VK_NUMPAD2: button = io::KEYBOARD_BUTTON_NUMPAD2; break;
                case VK_NUMPAD3: button = io::KEYBOARD_BUTTON_NUMPAD3; break;
                case VK_NUMPAD4: button = io::KEYBOARD_BUTTON_NUMPAD4; break;
                case VK_NUMPAD5: button = io::KEYBOARD_BUTTON_NUMPAD5; break;
                case VK_NUMPAD6: button = io::KEYBOARD_BUTTON_NUMPAD6; break;
                case VK_NUMPAD7: button = io::KEYBOARD_BUTTON_NUMPAD7; break;
                case VK_NUMPAD8: button = io::KEYBOARD_BUTTON_NUMPAD8; break;
                case VK_NUMPAD9: button = io::KEYBOARD_BUTTON_NUMPAD9; break;
                case VK_MULTIPLY: button = io::KEYBOARD_BUTTON_MULTIPLY; break;
                case VK_ADD: button = io::KEYBOARD_BUTTON_ADD; break;
                case VK_SEPARATOR: button = io::KEYBOARD_BUTTON_SEPARATOR; break;
                case VK_SUBTRACT: button = io::KEYBOARD_BUTTON_SUBTRACT; break;
                case VK_DECIMAL: button = io::KEYBOARD_BUTTON_DECIMAL; break;
                case VK_DIVIDE: button = io::KEYBOARD_BUTTON_DIVIDE; break;
                case VK_TAB: button = io::KEYBOARD_BUTTON_TAB; break;
                case VK_OEM_3: button = io::KEYBOARD_BUTTON_TILDE; break;
                case VK_INSERT: button = io::KEYBOARD_BUTTON_INSERT; break;
                case VK_LMENU: button = io::KEYBOARD_BUTTON_ALT; break;
                case VK_RMENU: button = io::KEYBOARD_BUTTON_ALTGR; break;
                default: break;
                }
            }

            if (button != io::BUTTON_NONE)
            {
                io::InputEvent event = {};
                event.type = io::InputEventType::Button;
                event.button = button;
                event.pressed = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
                io::PushInputEvent(event);
            }
            break;
        }
        case WM_MOUSEMOVE:
        {
            io::InputEvent event = {};
            event.type = io::InputEventType::MouseMove;
            event.position = float2(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
            io::PushInputEvent(event);
            break;
        }
        case WM_MOUSEWHEEL:
        {
            POINT point = { GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
            ScreenToClient(hwnd, &point);
            io::InputEvent event = {};
            event.type = io::InputEventType::MouseWheel;
            event.position = float2(static_cast<float>(point.x), static_cast<float>(point.y));
            event.value = static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
            io::PushInputEvent(event);
            break;
        }
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        {
            io::InputEvent event = {};
            event.type = io::InputEventType::Button;
            event.pressed = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN;
            event.position = float2(static_cast<float>(GET_X_LPARAM(lparam)), static_cast<float>(GET_Y_LPARAM(lparam)));
            if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP)
            {
                event.button = io::MOUSE_BUTTON_LEFT;
            }
            else if (message == WM_RBUTTONDOWN || message == WM_RBUTTONUP)
            {
                event.button = io::MOUSE_BUTTON_RIGHT;
            }
            else
            {
                event.button = io::MOUSE_BUTTON_MIDDLE;
            }
            io::PushInputEvent(event);
            break;
        }
        case WM_KILLFOCUS:
        {
            io::InputEvent event = {};
            event.type = io::InputEventType::FocusLost;
            io::PushInputEvent(event);
            break;
        }
        case WM_DESTROY:
            should_post_quit = true;
            message_handled = true;
            break;
        case WM_SIZE:
            should_update_size = true;
            message_handled = true;
            break;
        default:
            break;
        }

        if (window && window->DispatchPlatformMessage(hwnd, message, static_cast<Size>(wparam), static_cast<Size>(lparam)))
        {
            return 1;
        }

        if (should_post_quit)
        {
            PostQuitMessage(0);
            return 0;
        }

        if (should_update_size)
        {
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
        }

        if (message_handled)
        {
            return 0;
        }

        return DefWindowProcW(hwnd, message, wparam, lparam);
    }
}
#endif
