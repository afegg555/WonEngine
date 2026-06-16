#pragma once
#include "Resource.h"
#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::resource
{
    struct Sound : public Resource
    {
        uint32 sample_rate = 0;
        uint32 channel_count = 0;
        Vector<float> samples;

        bool IsValid() const override
        {
            return sample_rate > 0 && channel_count > 0 && !samples.empty();
        }
    };

    // Loads a WAV file from disk and returns a cached shared_ptr when possible.
    // The cache key is the normalized file path.
    WONENGINE_API std::shared_ptr<Sound> LoadSoundFile(const String& path);

    WONENGINE_API void ClearSoundCache();
    WONENGINE_API Size GetSoundCacheSize();
}
