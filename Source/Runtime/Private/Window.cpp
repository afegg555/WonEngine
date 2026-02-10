#include "Window.h"

#if defined(_WIN32)
#include "WindowWin32.h"
#endif

namespace won::platform
{
    std::shared_ptr<Window> CreateNativeWindow(const WindowDesc& desc)
    {
#if defined(_WIN32)
        return std::make_shared<WindowWin32>(desc);
#else
        (void)desc;
        return nullptr;
#endif
    }
}
