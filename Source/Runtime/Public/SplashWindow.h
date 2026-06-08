#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::platform
{
    struct SplashWindowColor
    {
        uint8 r = 0;
        uint8 g = 0;
        uint8 b = 0;
    };

    struct SplashWindowStyle
    {
        SplashWindowColor background_color = { 22, 24, 29 };
        SplashWindowColor title_color = { 238, 241, 245 };
        SplashWindowColor status_color = { 165, 172, 184 };
        const char* font_name = "Segoe UI";
        int title_font_height = 32;
        int status_font_height = 16;
        int horizontal_padding = 32;
        int title_top = 78;
        int title_height = 46;
        int status_top = 164;
        int status_height = 34;
    };

    struct SplashWindowDesc
    {
        const char* title = "Won Engine";
        const char* status = "Starting...";
        const char* image_path = nullptr;
        int width = 480;
        int height = 260;
        bool visible = true;
        SplashWindowStyle style;
    };

    // note : SplashWindow is separate from Window and does not own a renderer or swapchain.
    class SplashWindow
    {
    public:
        virtual ~SplashWindow() = default;

        virtual void* GetNativeHandle() const = 0;
        virtual void Show() = 0;
        virtual void Close() = 0;
        virtual void SetStatus(const char* status) = 0;
    };

    WONENGINE_API std::shared_ptr<SplashWindow> CreateSplashWindow(const SplashWindowDesc& desc);
}
