#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::resource
{
    struct Resource
    {
        virtual bool IsValid() const = 0;
    };

    struct Image : public Resource
    {
        int32 width = 0;
        int32 height = 0;
        int32 channels = 0;
        Vector<uint8> pixels;

        bool IsValid() const override
        {
            return width > 0 && height > 0 && channels > 0 && !pixels.empty();
        }
    };

    // Loads an image from disk and returns a cached shared_ptr when possible.
    // The cache key is the normalized file path.
    WONENGINE_API std::shared_ptr<Image> LoadImage(const String& path, int32 desired_channels = 4);

    WONENGINE_API void ClearImageCache();
    WONENGINE_API Size GetImageCacheSize();
}

