#pragma once
#include "RuntimeExport.h"

#include <memory>

namespace won::platform
{
    struct SwapchainDesc
    {
        const char* title = "WonEngine";
        int width = 1280;
        int height = 720;
        bool resizable = true;
        bool visible = true;
    };

    class WONENGINE_API Swapchain
    {
    public:
        virtual ~Swapchain() = default;

        virtual void* GetNativeHandle() const = 0;
        virtual void Show() = 0;
        virtual void Hide() = 0;
        virtual void SetTitle(const char* title) = 0;
        virtual void Resize(int width, int height) = 0;
        virtual int GetWidth() const = 0;
        virtual int GetHeight() const = 0;
    };

    WONENGINE_API std::shared_ptr<Swapchain> CreateSwapchain(const SwapchainDesc& desc);
}
