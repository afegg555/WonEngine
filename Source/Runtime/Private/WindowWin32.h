#pragma once
#include "Window.h"
#include "Platform.h"

namespace won::platform
{
    class WindowWin32 final : public Window
    {
    public:
        explicit WindowWin32(const WindowDesc& desc);
        ~WindowWin32() override;

        void* GetNativeHandle() const override;
        void Show() override;
        void Hide() override;
        void SetTitle(const char* title) override;
        void Resize(int width, int height) override;
        int GetWidth() const override;
        int GetHeight() const override;
        bool IsFocused() const override;
        bool IsMinimized() const override;
        bool ConsumePendingResize() override;

        static LRESULT CALLBACK WindowProc(HWND, UINT, WPARAM, LPARAM);
    private:
        WindowType hwnd = nullptr;
        int width = 0;
        int height = 0;
        bool is_minimized = false;
        bool has_pending_resize = false;
    };
}
