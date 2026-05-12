#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#include <functional>
#include <memory>

namespace won::rendering
{
    class RHISwapchain;
}

namespace won::platform
{
    using PlatformMessageHandler = std::function<bool(void* hwnd, uint32 message, Size wparam, Size lparam)>;

    struct WindowDesc
    {
        const char* title = "WonEngine";
        int width = 1280;
        int height = 720;
        bool fullscreen = false;
        bool resizable = true;
        bool use_title_bar = false;
        bool visible = true;
    };

    class Window
    {
    public:
        virtual ~Window() = default;

        virtual void* GetNativeHandle() const = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual void SetTitle(const char* title) = 0;
        virtual void Resize(int width, int height) = 0;
        virtual void SetPosition(int x, int y) = 0;
        virtual bool GetPosition(int& out_x, int& out_y) const = 0;
        virtual bool GetCursorPosition(int& out_x, int& out_y) const = 0;
        virtual void Minimize() = 0;
        virtual void Maximize() = 0;
        virtual void Restore() = 0;
        virtual void Close() = 0;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
        virtual bool IsFocused() const = 0;
        virtual bool IsMinimized() const = 0;
        virtual bool IsMaximized() const = 0;
        virtual bool ConsumePendingResize() = 0;

        void SetPlatformMessageHandler(PlatformMessageHandler new_handler)
        {
            platform_message_handler = std::move(new_handler);
        }

        bool DispatchPlatformMessage(void* hwnd, uint32 message, Size wparam, Size lparam)
        {
            if (!platform_message_handler)
            {
                return false;
            }

            return platform_message_handler(hwnd, message, wparam, lparam);
        }

        void SetRHISwapchain(const std::shared_ptr<rendering::RHISwapchain>& new_swapchain)
        {
            rhi_swapchain = new_swapchain;
        }

        std::shared_ptr<rendering::RHISwapchain> GetRHISwapchain() const
        {
            return rhi_swapchain;
        }

    private:
        PlatformMessageHandler platform_message_handler;
        std::shared_ptr<rendering::RHISwapchain> rhi_swapchain;
    };

    WONENGINE_API std::shared_ptr<Window> CreateNativeWindow(const WindowDesc& desc);
}
