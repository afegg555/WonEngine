#include "Swapchain.h"

#if defined(_WIN32)
#include "SwapChainWindows.h"
#endif

namespace won::platform
{
    std::shared_ptr<Swapchain> CreateSwapchain(const SwapchainDesc& desc)
    {
#if defined(_WIN32)
        return std::make_shared<SwapChainWindows>(desc);
#else
        (void)desc;
        return nullptr;
#endif
    }
}
