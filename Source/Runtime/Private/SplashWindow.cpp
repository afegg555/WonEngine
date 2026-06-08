#include "SplashWindow.h"

#if defined(_WIN32)
#include "SplashWindowWin32.h"
#endif

namespace won::platform
{
    std::shared_ptr<SplashWindow> CreateSplashWindow(const SplashWindowDesc& desc)
    {
#if defined(_WIN32)
        return std::make_shared<SplashWindowWin32>(desc);
#else
        (void)desc;
        return nullptr;
#endif
    }
}
