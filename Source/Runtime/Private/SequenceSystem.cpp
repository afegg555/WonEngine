#include "SequenceSystem.h"

#include "CameraComponent.h"
#include "Scene.h"
#include "SequenceComponent.h"
#include "TransformComponent.h"

namespace won::ecs
{
    void SequenceSystem::Update(Scene& scene, float delta_time)
    {
        auto sequence_array = scene.GetComponentArray<SequenceComponent>().get();
        if (!sequence_array)
        {
            return;
        }

		// not parallelized, sequences are usually few and small, and they may modify the same components
        for (Size index = 0; index < sequence_array->GetSize(); ++index)
        {
            SequenceComponent& sequence = sequence_array->data[index];
            if (!sequence.IsEnabled())
            {
                continue;
            }

            const Entity entity = sequence_array->index_to_entity[index];
            if (!sequence.started)
            {
                sequence.started = true;
                if (sequence.IsPlayOnStart())
                {
                    sequence.SetPlaying(true);
                    sequence.time = 0.0f;
                    sequence.event_scan_time = -1.0f;
                }
            }
            if (!sequence.IsPlaying())
            {
                sequence.cut_camera = INVALID_ENTITY;
                continue;
            }

            float duration = sequence.duration;
            if (duration <= 0.0f)
            {
                for (const SequenceTrack& track : sequence.tracks)
                {
                    if (!track.keys.empty())
                    {
                        duration = (std::max)(duration, track.keys.back().time);
                    }
                }
            }

            const float advanced_time = sequence.time + delta_time;
            const float new_time = sequence.IsLoop() ? math::Wrap(advanced_time, duration) : math::Clamp(advanced_time, 0.0f, duration);
            const float scan_from = sequence.event_scan_time;

            auto sample_value = [new_time](const SequenceTrack& track) -> float4
            {
                if (new_time <= track.keys.front().time)
                {
                    return track.keys.front().value;
                }
                if (new_time >= track.keys.back().time)
                {
                    return track.keys.back().value;
                }

                Size upper = 1;
                while (upper < track.keys.size() && track.keys[upper].time < new_time)
                {
                    ++upper;
                }
                const SequenceKey& from = track.keys[upper - 1];
                const SequenceKey& to = track.keys[upper];
                const float span = to.time - from.time;
                const float alpha = span > 0.0f ? (new_time - from.time) / span : 0.0f;

                float4 blended = {};
                if (track.type == SequenceTrackType::Rotation)
                {
                    XMStoreFloat4(&blended, XMQuaternionNormalize(XMQuaternionSlerp(XMLoadFloat4(&from.value), XMLoadFloat4(&to.value), alpha)));
                }
                else
                {
                    XMStoreFloat4(&blended, XMVectorLerp(XMLoadFloat4(&from.value), XMLoadFloat4(&to.value), alpha));
                }
                return blended;
            };

            Entity cut_camera = INVALID_ENTITY;
            for (const SequenceTrack& track : sequence.tracks)
            {
                if (track.keys.empty())
                {
                    continue;
                }

                switch (track.type)
                {
                case SequenceTrackType::Position:
                {
                    TransformComponent* transform = scene.GetComponent<TransformComponent>(track.target);
                    if (transform)
                    {
                        const float4 value = sample_value(track);
                        transform->position = { value.x, value.y, value.z };
                        transform->SetDirty();
                    }
                    break;
                }
                case SequenceTrackType::Rotation:
                {
                    TransformComponent* transform = scene.GetComponent<TransformComponent>(track.target);
                    if (transform)
                    {
                        transform->rotation = sample_value(track);
                        transform->SetDirty();
                    }
                    break;
                }
                case SequenceTrackType::CameraFov:
                {
                    CameraComponent* camera = scene.GetComponent<CameraComponent>(track.target);
                    if (camera)
                    {
                        camera->SetFOV_Y(sample_value(track).x);
                    }
                    break;
                }
                case SequenceTrackType::CameraSwitch:
                {
                    Entity sampled = INVALID_ENTITY;
                    for (const SequenceKey& key : track.keys)
                    {
                        if (key.time > new_time)
                        {
                            break;
                        }
                        sampled = key.camera;
                    }
                    if (sampled != INVALID_ENTITY)
                    {
                        cut_camera = sampled;
                    }
                    break;
                }
                case SequenceTrackType::Event:
                {
                    auto emit = [&](float low, float high, bool inclusive_low)
                    {
                        for (const SequenceKey& key : track.keys)
                        {
                            const bool low_ok = inclusive_low ? (key.time >= low) : (key.time > low);
                            if (low_ok && key.time <= high)
                            {
                                scene.QueueSequenceEvent(entity, key.event_name);
                            }
                        }
                    };

                    if (new_time < scan_from)
                    {
                        emit(scan_from, duration, false);
                        emit(0.0f, new_time, true);
                    }
                    else if (scan_from < 0.0f)
                    {
                        emit(0.0f, new_time, true);
                    }
                    else
                    {
                        emit(scan_from, new_time, false);
                    }
                    break;
                }
                }
            }

            sequence.cut_camera = cut_camera;
            sequence.time = new_time;
            sequence.event_scan_time = new_time;

            if (!sequence.IsLoop() && advanced_time >= duration)
            {
                sequence.SetPlaying(false);
            }
        }
    }
}
