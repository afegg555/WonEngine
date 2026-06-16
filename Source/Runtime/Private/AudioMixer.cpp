#include "AudioMixer.h"
#include "MathUtils.h"

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace DirectX;

namespace won::audio
{
    AudioMixer::AudioMixer(uint32 in_sample_rate, uint32 in_channel_count)
        : sample_rate(in_sample_rate)
        , channel_count(in_channel_count)
    {
        voice_slots.reserve(max_voice_slots);
    }

    void AudioMixer::SetFormat(uint32 in_sample_rate, uint32 in_channel_count)
    {
        std::lock_guard<std::mutex> lock(mixer_mutex);
        sample_rate = in_sample_rate;
        channel_count = in_channel_count;
    }

    VoiceHandle AudioMixer::Play(const resource::Sound& sound, const VoiceParams& params)
    {
        std::lock_guard<std::mutex> lock(mixer_mutex);

        for (VoiceSlot& slot : voice_slots)
        {
            if (!slot.active)
            {
                slot.active = true;
                slot.sound = &sound;
                slot.params = params;
                slot.cursor = 0.0;
                slot.handle = next_voice_handle++;
                if (next_voice_handle == invalid_voice_handle)
                    next_voice_handle = 1;
                return slot.handle;
            }
        }

		// add a new one if we haven't reached the max limit(lazy init)
        if (voice_slots.size() < max_voice_slots)
        {
            VoiceSlot& slot = voice_slots.emplace_back();
            slot.active = true;
            slot.sound = &sound;
            slot.params = params;
            slot.cursor = 0.0;
            slot.handle = next_voice_handle++;
            if (next_voice_handle == invalid_voice_handle)
                next_voice_handle = 1;
            return slot.handle;
        }

        return invalid_voice_handle;
    }

    void AudioMixer::Stop(VoiceHandle handle)
    {
        std::lock_guard<std::mutex> lock(mixer_mutex);
        for (VoiceSlot& slot : voice_slots)
        {
            if (slot.active && slot.handle == handle)
            {
                slot.active = false;
                slot.sound = nullptr;
                return;
            }
        }
    }

    void AudioMixer::StopAll()
    {
        std::lock_guard<std::mutex> lock(mixer_mutex);
        for (VoiceSlot& slot : voice_slots)
        {
            slot.active = false;
            slot.sound = nullptr;
        }
    }

    void AudioMixer::UpdateParams(VoiceHandle handle, const VoiceParams& params)
    {
        std::lock_guard<std::mutex> lock(mixer_mutex);
        for (VoiceSlot& slot : voice_slots)
        {
            if (slot.active && slot.handle == handle)
            {
                slot.params = params;
                return;
            }
        }
    }

    bool AudioMixer::IsPlaying(VoiceHandle handle) const
    {
        std::lock_guard<std::mutex> lock(mixer_mutex);
        for (const VoiceSlot& slot : voice_slots)
        {
            if (slot.active && slot.handle == handle)
                return true;
        }
        return false;
    }

    void AudioMixer::SetListener(const ListenerState& state)
    {
        std::lock_guard<std::mutex> lock(mixer_mutex);
        listener_state = state;
    }

    void AudioMixer::MixFrames(float* output, uint32 frame_count)
    {
        std::memset(output, 0, frame_count * channel_count * sizeof(float));

        std::lock_guard<std::mutex> lock(mixer_mutex);

        const XMVECTOR listener_pos = XMLoadFloat3(&listener_state.position);
        const XMVECTOR listener_rot = XMLoadFloat4(&listener_state.rotation);
        const XMVECTOR right = XMMatrixRotationQuaternion(listener_rot).r[0];

        for (VoiceSlot& slot : voice_slots)
        {
            if (!slot.active || !slot.sound || slot.sound->samples.empty())
                continue;

            const resource::Sound* sound = slot.sound;
            const uint32 total_frames = static_cast<uint32>(sound->samples.size() / sound->channel_count);
            const double pitch_ratio = (double)sound->sample_rate / (double)sample_rate * (double)(std::max)(0.0001f, slot.params.pitch);

            float attenuation = 1.0f;
            float panning = 0.0f;

            if (slot.params.spatial_3d)
            {
                const XMVECTOR source_pos = XMLoadFloat3(&slot.params.position);
                const XMVECTOR diff = XMVectorSubtract(source_pos, listener_pos);
                const float distance = XMVectorGetX(XMVector3Length(diff));

                const float min_d = (std::max)(0.001f, slot.params.min_distance);
                const float max_d = (std::max)(min_d, slot.params.max_distance);
                if (distance > min_d)
                {
                    attenuation = distance >= max_d ? 0.0f : 1.0f - (distance - min_d) / (max_d - min_d);
                }

                if (distance > 0.0f)
                {
                    panning = XMVectorGetX(XMVector3Dot(XMVector3Normalize(diff), right));
                }
            }

            const float volume = (std::max)(0.0f, slot.params.volume) * attenuation;
            const float left_gain = (1.0f - panning) * 0.5f * volume;
            const float right_gain = (1.0f + panning) * 0.5f * volume;

            for (uint32 frame = 0; frame < frame_count; ++frame)
            {
                if (slot.cursor >= (double)total_frames)
                {
                    if (slot.params.loop)
                        slot.cursor = std::fmod(slot.cursor, (double)total_frames);
                    else
                    {
                        slot.active = false;
                        slot.sound = nullptr;
                        break;
                    }
                }

                const uint32 idx0 = static_cast<uint32>(slot.cursor);
                const uint32 idx1 = (idx0 + 1 < total_frames) ? idx0 + 1 : (slot.params.loop ? 0 : idx0);
                const float frac = static_cast<float>(slot.cursor - idx0);

                if (channel_count == 2)
                {
                    if (sound->channel_count == 1)
                    {
                        const float s = sound->samples[idx0] + (sound->samples[idx1] - sound->samples[idx0]) * frac;
                        output[frame * 2 + 0] += s * left_gain;
                        output[frame * 2 + 1] += s * right_gain;
                    }
                    else if (sound->channel_count >= 2)
                    {
                        const float sL = sound->samples[idx0 * 2 + 0] + (sound->samples[idx1 * 2 + 0] - sound->samples[idx0 * 2 + 0]) * frac;
                        const float sR = sound->samples[idx0 * 2 + 1] + (sound->samples[idx1 * 2 + 1] - sound->samples[idx0 * 2 + 1]) * frac;
                        output[frame * 2 + 0] += sL * left_gain;
                        output[frame * 2 + 1] += sR * right_gain;
                    }
                }
                else
                {
                    const float s = sound->channel_count == 1
                        ? sound->samples[idx0] + (sound->samples[idx1] - sound->samples[idx0]) * frac
                        : (sound->samples[idx0 * sound->channel_count] + sound->samples[idx0 * sound->channel_count + 1]) * 0.5f;
                    for (uint32 ch = 0; ch < channel_count; ++ch)
                        output[frame * channel_count + ch] += s * volume;
                }

                slot.cursor += pitch_ratio;
            }
        }
    }

    void AudioMixer::StaticMixCallback(float* output, uint32 frames, void* userdata)
    {
        static_cast<AudioMixer*>(userdata)->MixFrames(output, frames);
    }
}
