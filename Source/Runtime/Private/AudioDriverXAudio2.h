#pragma once
#include "AudioDriver.h"

#include <windows.h>
#include <xaudio2.h>
#include <array>

namespace won::audio
{
    class AudioDriverXAudio2 final : public IAudioDriver, public IXAudio2VoiceCallback
    {
    public:
        AudioDriverXAudio2() = default;
        ~AudioDriverXAudio2() override;

        bool Start(const AudioDriverDesc& desc, MixCallback callback, void* userdata) override;
        void Stop() override;
        uint32 GetSampleRate() const override { return desc.sample_rate; }
        uint32 GetChannelCount() const override { return desc.channel_count; }

        STDMETHOD_(void, OnVoiceProcessingPassStart)(UINT32) override {}
        STDMETHOD_(void, OnVoiceProcessingPassEnd)() override {}
        STDMETHOD_(void, OnStreamEnd)() override {}
        STDMETHOD_(void, OnBufferStart)(void*) override {}
        STDMETHOD_(void, OnBufferEnd)(void* pBufferContext) override;
        STDMETHOD_(void, OnLoopEnd)(void*) override {}
        STDMETHOD_(void, OnVoiceError)(void*, HRESULT) override {}

    private:
        void SubmitBuffer(uint32 buffer_index);

        IXAudio2* xaudio2 = nullptr;
        IXAudio2MasteringVoice* mastering_voice = nullptr;
        IXAudio2SourceVoice* source_voice = nullptr;

        AudioDriverDesc desc;
        MixCallback mix_callback = nullptr;
        void* user_data = nullptr;

        bool is_running = false;

        static constexpr uint32 buffer_count = 3;
        std::array<Vector<float>, buffer_count> audio_buffers;
    };
}
