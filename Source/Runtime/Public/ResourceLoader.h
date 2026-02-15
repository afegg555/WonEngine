#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::rendering
{
    class RHIDevice;
}

namespace won::resource
{
    struct WONENGINE_API Resource
    {
        virtual ~Resource() = default;
        virtual bool IsValid() const = 0;
        virtual bool CreateRenderData(const std::shared_ptr<rendering::RHIDevice>& device) = 0;
    };

    struct WONENGINE_API Image : public Resource
    {
        int32 width = 0;
        int32 height = 0;
        int32 channels = 0;
        Vector<uint8> pixels;

        bool IsValid() const override
        {
            return width > 0 && height > 0 && channels > 0 && !pixels.empty();
        }

        bool CreateRenderData(const std::shared_ptr<rendering::RHIDevice>& device) override
        {
            (void)device;
            return IsValid();
        }
    };

    // Loads an image from disk and returns a cached shared_ptr when possible.
    // The cache key is the normalized file path.
    WONENGINE_API std::shared_ptr<Image> LoadImage(const String& path, int32 desired_channels = 4);

    WONENGINE_API void ClearImageCache();
    WONENGINE_API Size GetImageCacheSize();
}
