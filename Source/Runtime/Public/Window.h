#pragma once
#include "RuntimeExport.h"

#include <memory>

namespace won::rendering
{
    class RHISwapchain;
}

namespace won::platform
{
    struct WindowDesc
    {
        const char* title = "WonEngine";
        int width = 1280;
        int height = 720;
        bool resizable = true;
        bool visible = true;
    };

    class WONENGINE_API Window
    {
    public:
        virtual ~Window() = default;

        virtual void* GetNativeHandle() const = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual void SetTitle(const char* title) = 0;
        virtual void Resize(int width, int height) = 0;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;

        void SetRHISwapchain(const std::shared_ptr<rendering::RHISwapchain>& new_swapchain)
        {
            rhi_swapchain = new_swapchain;
        }

        std::shared_ptr<rendering::RHISwapchain> GetRHISwapchain() const
        {
            return rhi_swapchain;
        }

    private:
        std::shared_ptr<rendering::RHISwapchain> rhi_swapchain;
    };

    WONENGINE_API std::shared_ptr<Window> CreateNativeWindow(const WindowDesc& desc);
}
