#include "Animation.h"

namespace won::resource
{
    void SampleAnimationPose(const AnimationClip& clip, float sample_time, const Skeleton& skeleton, Vector<BonePose>& out_pose)
    {
        const Size bone_count = skeleton.bones.size();
        out_pose.resize(bone_count);

        Vector<const AnimationChannel*> channels_by_bone;
        channels_by_bone.resize(bone_count, nullptr);
        for (const AnimationChannel& channel : clip.channels)
        {
            if (channel.bone_index < bone_count && channel.IsValid())
            {
                channels_by_bone[channel.bone_index] = &channel;
            }
        }

        for (Size bone_index = 0; bone_index < bone_count; ++bone_index)
        {
            const Bone& bone = skeleton.bones[bone_index];
            XMVECTOR bind_scale = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
            XMVECTOR bind_rotation = XMQuaternionIdentity();
            XMVECTOR bind_translation = XMVectorZero();
            XMMatrixDecompose(&bind_scale, &bind_rotation, &bind_translation, XMLoadFloat4x4(&bone.bind_local_transform));

            BonePose& pose = out_pose[bone_index];
            XMStoreFloat3(&pose.position, bind_translation);
            XMStoreFloat4(&pose.rotation, XMQuaternionNormalize(bind_rotation));
            XMStoreFloat3(&pose.scale, bind_scale);

            const AnimationChannel* channel = channels_by_bone[bone_index];
            if (!channel)
            {
                continue;
            }

            if (!channel->positions.empty())
            {
                if (sample_time <= channel->positions.front().time || channel->positions.size() == 1)
                {
                    pose.position = channel->positions.front().value;
                }
                else if (sample_time >= channel->positions.back().time)
                {
                    pose.position = channel->positions.back().value;
                }
                else
                {
                    for (Size key_index = 0; key_index + 1 < channel->positions.size(); ++key_index)
                    {
                        const auto& key0 = channel->positions[key_index];
                        const auto& key1 = channel->positions[key_index + 1];
                        if (sample_time >= key0.time && sample_time <= key1.time)
                        {
                            const float t = (sample_time - key0.time) / (key1.time - key0.time);
                            pose.position = math::Lerp(key0.value, key1.value, t);
                            break;
                        }
                    }
                }
            }

            if (!channel->rotations.empty())
            {
                if (sample_time <= channel->rotations.front().time || channel->rotations.size() == 1)
                {
                    pose.rotation = channel->rotations.front().value;
                }
                else if (sample_time >= channel->rotations.back().time)
                {
                    pose.rotation = channel->rotations.back().value;
                }
                else
                {
                    for (Size key_index = 0; key_index + 1 < channel->rotations.size(); ++key_index)
                    {
                        const auto& key0 = channel->rotations[key_index];
                        const auto& key1 = channel->rotations[key_index + 1];
                        if (sample_time >= key0.time && sample_time <= key1.time)
                        {
                            const float t = (sample_time - key0.time) / (key1.time - key0.time);
                            pose.rotation = math::Slerp(key0.value, key1.value, t);
                            break;
                        }
                    }
                }
            }

            if (!channel->scales.empty())
            {
                if (sample_time <= channel->scales.front().time || channel->scales.size() == 1)
                {
                    pose.scale = channel->scales.front().value;
                }
                else if (sample_time >= channel->scales.back().time)
                {
                    pose.scale = channel->scales.back().value;
                }
                else
                {
                    for (Size key_index = 0; key_index + 1 < channel->scales.size(); ++key_index)
                    {
                        const auto& key0 = channel->scales[key_index];
                        const auto& key1 = channel->scales[key_index + 1];
                        if (sample_time >= key0.time && sample_time <= key1.time)
                        {
                            const float t = (sample_time - key0.time) / (key1.time - key0.time);
                            pose.scale = math::Lerp(key0.value, key1.value, t);
                            break;
                        }
                    }
                }
            }
        }
    }
}
