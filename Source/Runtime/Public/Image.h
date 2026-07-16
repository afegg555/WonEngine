#pragma once
#include "RHIResource.h"
#include "Resource.h"
#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::resource
{
    struct Image : public Resource
    {
        struct RenderData
        {
            std::shared_ptr<rendering::RHIResource> texture;
            rendering::RHISubresourceHandle srv = {};
            rendering::RHIFormat format = rendering::RHIFormat::Unknown;
            uint32 mip_levels = 0;

            bool IsValid() const
            {
                return texture != nullptr && srv.IsValid();
            }
        };

        int32 width = 0;
        int32 height = 0;
        int32 channels = 0;
        uint32 mip_levels = 1;
        bool is_cube = false;
        rendering::RHIFormat format = rendering::RHIFormat::Unknown;
        Vector<uint8> pixels;
        RenderData render_data = {};

        bool IsValid() const override
        {
            return width > 0 && height > 0 && channels > 0 && !pixels.empty();
        }

        void ClearRenderData()
        {
            render_data = {};
        }
    };

    // Loads an image from disk and returns a cached shared_ptr when possible.
    // The cache key is the normalized file path.
    WONENGINE_API std::shared_ptr<Image> LoadImageFile(const String& path, int32 desired_channels = 4);
    WONENGINE_API std::shared_ptr<Image> LoadImageMemory(const uint8* data, Size size, int32 desired_channels = 4);

    WONENGINE_API void ClearImageCache();
    WONENGINE_API Size GetImageCacheSize();
}
