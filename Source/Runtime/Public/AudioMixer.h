#pragma once
#include "Types.h"
#include "RuntimeExport.h"
#include "Sound.h"

#include <mutex>

namespace won::audio
{
    using VoiceHandle = uint32;
    constexpr VoiceHandle invalid_voice_handle = 0xffffffff;
    constexpr uint32 max_voice_slots = 32;

    struct VoiceParams
    {
        float volume = 1.0f;
        float pitch = 1.0f;
        float3 position = { 0.0f, 0.0f, 0.0f };
        bool spatial_3d = false;
        bool loop = false;
        float min_distance = 1.0f;
        float max_distance = 20.0f;
        String submix = "";
    };

    struct ListenerState
    {
        float3 position = { 0.0f, 0.0f, 0.0f };
        float4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
    };

    class WONENGINE_API AudioMixer
    {
    public:
        AudioMixer(uint32 sample_rate, uint32 channel_count);
        ~AudioMixer() = default;

        void SetFormat(uint32 sample_rate, uint32 channel_count);

        VoiceHandle Play(const resource::Sound& sound, const VoiceParams& params);
        void Stop(VoiceHandle handle);
        void StopAll();
        void UpdateParams(VoiceHandle handle, const VoiceParams& params);
        bool IsPlaying(VoiceHandle handle) const;

        void SetListener(const ListenerState& state);
        void SetMasterVolume(float volume);
        float GetMasterVolume() const;
        void SetSubmixVolume(const String& name, float volume);
        float GetSubmixVolume(const String& name) const;
        void MixFrames(float* output, uint32 frame_count);

        static void StaticMixCallback(float* output, uint32 frames, void* userdata);

    private:
        struct VoiceSlot
        {
            bool active = false;
            const resource::Sound* sound = nullptr;
            VoiceParams params;
            double cursor = 0.0;
            VoiceHandle handle = invalid_voice_handle;
        };

        uint32 sample_rate;
        uint32 channel_count;

        mutable std::mutex mixer_mutex;
        Vector<VoiceSlot> voice_slots;
        uint32 next_voice_handle = 1;

        ListenerState listener_state;

        float master_volume = 1.0f;
        UnorderedMap<String, float> submix_volumes;
    };
}
