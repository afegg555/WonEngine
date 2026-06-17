#include "Sound.h"
#include "FileSystem.h"

#include <windows.h>
#include <cstring>
#include <mutex>

namespace won::resource
{
    namespace
    {
        constexpr uint16_t wave_format_pcm = 1;
        constexpr uint16_t wave_format_ieee_float = 3;

        std::mutex sound_cache_mutex;
        UnorderedMap<String, std::weak_ptr<Sound>> sound_cache;

        bool LoadWavFile(const String& path, Sound& out_sound)
        {
            io::FileData file_data;
            if (!io::ReadAllBytes(path, &file_data))
                return false;

            const uint8_t* bytes = file_data.bytes.data();
            const size_t size = file_data.bytes.size();

            if (size < 44)
                return false;
            if (std::memcmp(bytes, "RIFF", 4) != 0 || std::memcmp(bytes + 8, "WAVE", 4) != 0)
                return false;

            size_t offset = 12;
            WAVEFORMATEX wfx = {};
            const uint8_t* data_ptr = nullptr;
            uint32_t data_size = 0;

            while (offset + 8 <= size)
            {
                const char* chunk_id = reinterpret_cast<const char*>(bytes + offset);
                uint32_t chunk_size = *reinterpret_cast<const uint32_t*>(bytes + offset + 4);
                offset += 8;

                if (offset + chunk_size > size)
                    break;

                if (std::memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16)
                    std::memcpy(&wfx, bytes + offset, sizeof(WAVEFORMATEX) < chunk_size ? sizeof(WAVEFORMATEX) : chunk_size);
                else if (std::memcmp(chunk_id, "data", 4) == 0)
                {
                    data_ptr = bytes + offset;
                    data_size = chunk_size;
                }

                offset += chunk_size;
                if (chunk_size % 2 != 0)
                    ++offset;
            }

            if (!data_ptr || wfx.nChannels == 0 || wfx.wBitsPerSample == 0)
                return false;

            out_sound.sample_rate = wfx.nSamplesPerSec;
            out_sound.channel_count = wfx.nChannels;

            const uint32_t bytes_per_sample = wfx.wBitsPerSample / 8;
            const uint32_t total_samples = data_size / bytes_per_sample;
            out_sound.samples.resize(total_samples);

            if (wfx.wFormatTag == wave_format_ieee_float && wfx.wBitsPerSample == 32)
            {
                std::memcpy(out_sound.samples.data(), data_ptr, total_samples * sizeof(float));
            }
            else if (wfx.wFormatTag == wave_format_pcm)
            {
                if (wfx.wBitsPerSample == 16)
                {
                    const int16_t* src = reinterpret_cast<const int16_t*>(data_ptr);
                    for (uint32_t i = 0; i < total_samples; ++i)
                        out_sound.samples[i] = src[i] / 32768.0f;
                }
                else if (wfx.wBitsPerSample == 8)
                {
                    for (uint32_t i = 0; i < total_samples; ++i)
                        out_sound.samples[i] = (data_ptr[i] - 128) / 128.0f;
                }
                else if (wfx.wBitsPerSample == 24)
                {
                    for (uint32_t i = 0; i < total_samples; ++i)
                    {
                        int32_t val = (data_ptr[i * 3 + 2] << 16) | (data_ptr[i * 3 + 1] << 8) | data_ptr[i * 3];
                        if (val & 0x800000)
                            val |= 0xFF000000;
                        out_sound.samples[i] = val / 8388608.0f;
                    }
                }
                else if (wfx.wBitsPerSample == 32)
                {
                    const int32_t* src = reinterpret_cast<const int32_t*>(data_ptr);
                    for (uint32_t i = 0; i < total_samples; ++i)
                        out_sound.samples[i] = src[i] / 2147483648.0f;
                }
            }

            return !out_sound.samples.empty();
        }
    }

    std::shared_ptr<Sound> LoadSoundFile(const String& path)
    {
        if (path.empty())
            return nullptr;

        const String key = io::GetAbsolutePath(path);

        {
            std::lock_guard<std::mutex> lock(sound_cache_mutex);
            auto it = sound_cache.find(key);
            if (it != sound_cache.end())
            {
                if (auto existing = it->second.lock())
                    return existing;
            }
        }

        auto loaded = std::make_shared<Sound>();
        if (!LoadWavFile(path, *loaded))
            return nullptr;

        {
            std::lock_guard<std::mutex> lock(sound_cache_mutex);
            auto it = sound_cache.find(key);
            if (it != sound_cache.end())
            {
                if (auto existing = it->second.lock())
                    return existing;
            }
            sound_cache[key] = loaded;
        }

        return loaded;
    }

    void ClearSoundCache()
    {
        std::lock_guard<std::mutex> lock(sound_cache_mutex);
        sound_cache.clear();
    }

    Size GetSoundCacheSize()
    {
        std::lock_guard<std::mutex> lock(sound_cache_mutex);
        return sound_cache.size();
    }
}
