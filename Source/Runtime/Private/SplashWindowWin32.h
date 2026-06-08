#pragma once
#include "SplashWindow.h"
#include "Platform.h"

namespace Gdiplus
{
    class Bitmap;
}

namespace won::platform
{
    class SplashWindowWin32 final : public SplashWindow
    {
    public:
        explicit SplashWindowWin32(const SplashWindowDesc& desc);
        ~SplashWindowWin32() override;

        void* GetNativeHandle() const override;
        void Show() override;
        void Close() override;
        void SetStatus(const char* status) override;

        static LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam);

    private:
        void Paint();

        WindowType hwnd = nullptr;
        String title;
        String status;
        String image_path;
        String font_name;
        SplashWindowStyle style;
        std::unique_ptr<Gdiplus::Bitmap> image;
        ULONG_PTR gdiplus_token = 0;
        int width = 0;
        int height = 0;
    };
}
