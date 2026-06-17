#include "Image.h"
#include "FileSystem.h"

#include <cstring>
#include <mutex>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"

namespace won::resource
{
    namespace
    {
        std::mutex image_cache_mutex;
        UnorderedMap<String, std::weak_ptr<Image>> image_cache;

        String NormalizePathKey(const String& path)
        {
            return io::GetAbsolutePath(path);
        }

        std::shared_ptr<Image> LoadImageUncached(const String& path, int32 desired_channels)
        {
            io::FileData file_data;
            if (!io::ReadAllBytes(path, &file_data))
            {
                return nullptr;
            }

            return LoadImageMemory(file_data.bytes.data(), file_data.bytes.size(), desired_channels);
        }
    }

    std::shared_ptr<Image> LoadImageFile(const String& path, int32 desired_channels)
    {
        if (path.empty())
        {
            return nullptr;
        }

        const String key = NormalizePathKey(path);

        {
            std::lock_guard<std::mutex> lock(image_cache_mutex);
            auto it = image_cache.find(key);
            if (it != image_cache.end())
            {
                if (auto existing = it->second.lock())
                {
                    return existing;
                }
            }
        }

        // if there is no cache, or the cache is expired
        auto loaded = LoadImageUncached(path, desired_channels);
        if (!loaded)
        {
            return nullptr;
        }

        {
            std::lock_guard<std::mutex> lock(image_cache_mutex);
            auto it = image_cache.find(key);
            if (it != image_cache.end())
            {
                if (auto existing = it->second.lock())
                    return existing;
            }
            image_cache[key] = loaded;
        }

        return loaded;
    }

    std::shared_ptr<Image> LoadImageMemory(const uint8* data, Size size, int32 desired_channels)
    {
        if (!data || size == 0)
        {
            return nullptr;
        }

        int width = 0;
        int height = 0;
        int channels_in_file = 0;

        const int stb_desired_channels = (desired_channels <= 0) ? 0 : static_cast<int>(desired_channels);
        stbi_uc* pixels = stbi_load_from_memory(
            reinterpret_cast<const stbi_uc*>(data),
            static_cast<int>(size),
            &width,
            &height,
            &channels_in_file,
            stb_desired_channels);

        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            if (pixels != nullptr)
            {
                stbi_image_free(pixels);
            }
            return nullptr;
        }

        const int final_channels = (stb_desired_channels == 0) ? channels_in_file : stb_desired_channels;
        const Size pixel_count = static_cast<Size>(width) * static_cast<Size>(height) * static_cast<Size>(final_channels);

        auto image = std::make_shared<Image>();
        image->width = width;
        image->height = height;
        image->channels = final_channels;
        image->pixels.resize(pixel_count);
        std::memcpy(image->pixels.data(), pixels, pixel_count);

        stbi_image_free(pixels);
        return image;
    }

    void ClearImageCache()
    {
        std::lock_guard<std::mutex> lock(image_cache_mutex);
        image_cache.clear();
    }

    Size GetImageCacheSize()
    {
        std::lock_guard<std::mutex> lock(image_cache_mutex);
        return image_cache.size();
    }
}
