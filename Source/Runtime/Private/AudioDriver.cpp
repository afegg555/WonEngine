#include "AudioDriver.h"

#if defined(_WIN32)
#include "AudioDriverXAudio2.h"
#endif

namespace won::audio
{
    std::unique_ptr<IAudioDriver> CreateAudioDriver()
    {
#if defined(_WIN32)
        return std::make_unique<AudioDriverXAudio2>();
#else
        return nullptr;
#endif
    }
}
