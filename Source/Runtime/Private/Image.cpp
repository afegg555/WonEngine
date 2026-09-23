#include "Image.h"
#include "FileSystem.h"
#include "Backlog.h"

#include <cstring>
#include <mutex>

#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb/stb_image_write.h"

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

            std::shared_ptr<Image> image = LoadImageMemory(file_data.bytes.data(), file_data.bytes.size(), desired_channels);
            if (image)
            {
                image->name = path;
            }
            return image;
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

    bool SaveImageFile(const Image& image, const String& path)
    {
        if (!image.IsValid())
        {
            return false;
        }

        String extension = io::GetExtension(path);
        for (char& character : extension)
        {
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }

        const int32 width = image.width;
        const int32 height = image.height;
        Vector<uint8> converted;
        const uint8* data = nullptr;
        int32 component_count = 0;

        if (image.format == rendering::RHIFormat::R16Float)
        {
            const Size pixel_count = static_cast<Size>(width) * height;
            if (image.pixels.size() < pixel_count * sizeof(uint16))
            {
                return false;
            }
            converted.resize(pixel_count);
            const uint16* source = reinterpret_cast<const uint16*>(image.pixels.data());
            for (Size i = 0; i < pixel_count; ++i)
            {
                const uint16 half = source[i];
                const uint32 exponent = (half >> 10) & 0x1Fu;
                const uint32 mantissa = half & 0x3FFu;
                uint32 bits;
                if (exponent == 0u)
                {
                    bits = static_cast<uint32>(half & 0x8000u) << 16;
                }
                else if (exponent == 0x1Fu)
                {
                    bits = (static_cast<uint32>(half & 0x8000u) << 16) | 0x7F800000u | (mantissa << 13);
                }
                else
                {
                    bits = (static_cast<uint32>(half & 0x8000u) << 16) | ((exponent + 112u) << 23) | (mantissa << 13);
                }
                float value;
                std::memcpy(&value, &bits, sizeof(value));
                const float clamped = value < 0.0f ? 0.0f : (value > 1.0f ? 1.0f : value);
                converted[i] = static_cast<uint8>(clamped * 255.0f + 0.5f);
            }
            data = converted.data();
            component_count = 1;
        }
        else if (image.channels >= 1 && image.channels <= 4
            && image.pixels.size() >= static_cast<Size>(width) * height * image.channels)
        {
            data = image.pixels.data();
            component_count = image.channels;
        }
        else
        {
            backlog::Post("SaveImageFile: unsupported image format for " + path, backlog::LogLevel::Error);
            return false;
        }

        int result = 0;
        if (extension == "png")
        {
            result = stbi_write_png(path.c_str(), width, height, component_count, data, width * component_count);
        }
        else if (extension == "bmp")
        {
            result = stbi_write_bmp(path.c_str(), width, height, component_count, data);
        }
        else if (extension == "tga")
        {
            result = stbi_write_tga(path.c_str(), width, height, component_count, data);
        }
        else if (extension == "jpg" || extension == "jpeg")
        {
            result = stbi_write_jpg(path.c_str(), width, height, component_count, data, 90);
        }
        else
        {
            backlog::Post("SaveImageFile: unsupported file extension for " + path, backlog::LogLevel::Error);
            return false;
        }

        if (result == 0)
        {
            backlog::Post("SaveImageFile: failed to write " + path, backlog::LogLevel::Error);
            return false;
        }
        return true;
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
