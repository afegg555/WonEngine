#include "ResourceLoader.h"
#include "Types.h"
#include "FileSystem.h"

#include <filesystem>
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
            std::error_code error;
            std::filesystem::path fs_path = std::filesystem::u8path(path);
            fs_path = std::filesystem::absolute(fs_path, error);
            if (!error)
            {
                fs_path = fs_path.lexically_normal();
            }
            return fs_path.u8string();
        }

        std::shared_ptr<Image> LoadImageUncached(const String& path, int32 desired_channels)
        {
            io::FileData file_data;
            if (!io::ReadAllBytes(path, &file_data))
            {
                return nullptr;
            }

            int width = 0;
            int height = 0;
            int channels_in_file = 0;

            const int stb_desired_channels = (desired_channels <= 0) ? 0 : static_cast<int>(desired_channels);
            stbi_uc* pixels = stbi_load_from_memory(
                reinterpret_cast<const stbi_uc*>(file_data.bytes.data()),
                static_cast<int>(file_data.bytes.size()),
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
    }

    std::shared_ptr<Image> LoadImage(const String& path, int32 desired_channels)
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
            image_cache[key] = loaded;
        }

        return loaded;
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
