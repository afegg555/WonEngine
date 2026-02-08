#pragma once
#include "Swapchain.h"
#include "Platform.h"

namespace won::platform
{
    class SwapChainWindows final : public Swapchain
    {
    public:
        explicit SwapChainWindows(const SwapchainDesc& desc);
        ~SwapChainWindows() override;

        void* GetNativeHandle() const override;
        void Show() override;
        void Hide() override;
        void SetTitle(const char* title) override;
        void Resize(int width, int height) override;
        int GetWidth() const override;
        int GetHeight() const override;

    private:
        WindowType hwnd = nullptr;
        int width = 0;
        int height = 0;
    };
}

