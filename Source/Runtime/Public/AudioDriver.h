#pragma once
#include "Types.h"
#include "RuntimeExport.h"

#include <memory>

namespace won::audio
{
    struct AudioDriverDesc
    {
		uint32 sample_rate = 44100; // e.g. 44100 for CD quality, 48000 for common game audio quality, 96000 or higher for high-end audio
		uint32 channel_count = 2; // e.g. 1 for mono, 2 for stereo
		uint32 buffer_frames = 1024; // chunk size in frames (1 frame is 1 set of samples for all channels). lower values reduce latency but increase CPU overhead. 1024 is common choice
    };

    using MixCallback = void(*)(float* output, uint32 frames, void* userdata);

    class WONENGINE_API IAudioDriver
    {
    public:
        virtual ~IAudioDriver() = default;
        virtual bool Start(const AudioDriverDesc& desc, MixCallback callback, void* userdata) = 0;
        virtual void Stop() = 0;
        virtual uint32 GetSampleRate() const = 0;
        virtual uint32 GetChannelCount() const = 0;
    };

    WONENGINE_API std::unique_ptr<IAudioDriver> CreateAudioDriver();
}
