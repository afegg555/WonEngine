#include "AnimationUpdateSystem.h"

#include "AnimationComponent.h"
#include "GeometryComponent.h"
#include "JobSystem.h"
#include "MathUtils.h"
#include "Mesh.h"
#include "Scene.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace won::ecs
{
    void AnimationUpdateSystem::Update(Scene& scene, float delta_time)
    {
        auto animation_array = scene.GetComponentArray<AnimationComponent>().get();
        auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();
        Scene::RenderData& render_data = scene.GetRenderData();
        render_data.shader_bone_matrices.clear();
        if (!animation_array || !geometry_array)
        {
            return;
        }

        jobsystem::Context sub_ctx;
        std::atomic<uint32> total_bone_count{ 0 };

        jobsystem::Dispatch(sub_ctx, (uint32)animation_array->GetSize(), jobsystem::groupsize, [&](jobsystem::JobArgs args) {
            const Entity entity = animation_array->index_to_entity[args.job_index];
            AnimationComponent& animation = animation_array->data[args.job_index];
            animation.bone_matrix_offset = 0;
            if (!geometry_array->HasData(entity))
            {
                animation.bone_matrices.clear();
                return;
            }

            const GeometryComponent& geometry = geometry_array->GetData(entity);
            if (geometry.mesh && animation.clips.empty() && !geometry.mesh->animation_clips.empty())
            {
                animation.clips = geometry.mesh->animation_clips;
            }
            if (!geometry.mesh || !geometry.mesh->skeleton || !geometry.mesh->skeleton->IsValid() || animation.clips.empty())
            {
                animation.bone_matrices.clear();
                return;
            }

            const Size clip_index = animation.current_clip_index < animation.clips.size() ? animation.current_clip_index : 0;
            const std::shared_ptr<resource::AnimationClip>& clip = animation.clips[clip_index];
            if (!clip || !clip->IsValid())
            {
                animation.bone_matrices.clear();
                return;
            }

            const Size bone_count = geometry.mesh->skeleton->bones.size();
            if (bone_count == 0)
            {
                animation.bone_matrices.clear();
                return;
            }

            animation.bone_matrix_offset = total_bone_count.fetch_add(static_cast<uint32>(bone_count), std::memory_order_relaxed);
        });

        jobsystem::Wait(sub_ctx);

        const uint32 final_bone_count = total_bone_count.load(std::memory_order_relaxed);
        if (final_bone_count == 0)
        {
            return;
        }

        render_data.shader_bone_matrices.resize(static_cast<Size>(final_bone_count) * 4);

        jobsystem::Dispatch(sub_ctx, (uint32)animation_array->GetSize(), jobsystem::groupsize, [&](jobsystem::JobArgs args) {
            const Entity entity = animation_array->index_to_entity[args.job_index];
            AnimationComponent& animation = animation_array->data[args.job_index];
            if (!geometry_array->HasData(entity))
            {
                animation.bone_matrices.clear();
                animation.bone_matrix_offset = 0;
                return;
            }

            GeometryComponent& geometry = geometry_array->GetData(entity);
            if (geometry.mesh && animation.clips.empty() && !geometry.mesh->animation_clips.empty())
            {
                animation.clips = geometry.mesh->animation_clips;
            }
            if (!geometry.mesh || !geometry.mesh->skeleton || !geometry.mesh->skeleton->IsValid() || animation.clips.empty())
            {
                animation.bone_matrices.clear();
                animation.bone_matrix_offset = 0;
                return;
            }

            if (animation.current_clip_index >= animation.clips.size())
            {
                animation.current_clip_index = 0;
            }

            const std::shared_ptr<resource::AnimationClip>& clip = animation.clips[animation.current_clip_index];
            if (!clip || !clip->IsValid())
            {
                animation.bone_matrices.clear();
                animation.bone_matrix_offset = 0;
                return;
            }

            const float ticks_per_second = clip->ticks_per_second > 0.0f ? clip->ticks_per_second : 1.0f;
            const float duration_seconds = clip->duration / ticks_per_second;
            if (animation.playing)
            {
                animation.time += delta_time * animation.speed;
                if (duration_seconds > 0.0f)
                {
                    if (animation.loop)
                    {
                        animation.time = std::fmod(animation.time, duration_seconds);
                        if (animation.time < 0.0f)
                        {
                            animation.time += duration_seconds;
                        }
                    }
                    else
                    {
                        animation.time = std::clamp(animation.time, 0.0f, duration_seconds);
                    }
                }
                animation.bone_matrices_dirty = true;
            }

            const float sample_time = animation.time * ticks_per_second;
            resource::Skeleton& skeleton = *geometry.mesh->skeleton;
            const Size bone_count = skeleton.bones.size();
            if (bone_count == 0)
            {
                animation.bone_matrices.clear();
                animation.bone_matrix_offset = 0;
                return;
            }

            const uint32 current_bone_matrix_offset = animation.bone_matrix_offset;

            Vector<const resource::AnimationChannel*> channels_by_bone;
            channels_by_bone.resize(bone_count, nullptr);
            for (const resource::AnimationChannel& channel : clip->channels)
            {
                if (channel.bone_index < bone_count && channel.IsValid())
                {
                    channels_by_bone[channel.bone_index] = &channel;
                }
            }

            Vector<float4x4> local_matrices;
            Vector<float4x4> global_matrices;
            local_matrices.resize(bone_count, math::IDENTITY_MATRIX);
            global_matrices.resize(bone_count, math::IDENTITY_MATRIX);
            animation.bone_matrices.resize(bone_count, math::IDENTITY_MATRIX);

            for (Size bone_index = 0; bone_index < bone_count; ++bone_index)
            {
                const resource::Bone& bone = skeleton.bones[bone_index];
                XMVECTOR bind_scale = XMVectorSet(1.0f, 1.0f, 1.0f, 0.0f);
                XMVECTOR bind_rotation = XMQuaternionIdentity();
                XMVECTOR bind_translation = XMVectorZero();
                XMMatrixDecompose(&bind_scale, &bind_rotation, &bind_translation, XMLoadFloat4x4(&bone.bind_local_transform));

                float3 position = {};
                float4 rotation = {};
                float3 scale = {};
                XMStoreFloat3(&position, bind_translation);
                XMStoreFloat4(&rotation, XMQuaternionNormalize(bind_rotation));
                XMStoreFloat3(&scale, bind_scale);

                const resource::AnimationChannel* channel = channels_by_bone[bone_index];
                if (channel)
                {
                    if (!channel->positions.empty())
                    {
                        if (sample_time <= channel->positions.front().time || channel->positions.size() == 1)
                        {
                            position = channel->positions.front().value;
                        }
                        else if (sample_time >= channel->positions.back().time)
                        {
                            position = channel->positions.back().value;
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
                                    position = math::Lerp(key0.value, key1.value, t);
                                    break;
                                }
                            }
                        }
                    }

                    if (!channel->rotations.empty())
                    {
                        if (sample_time <= channel->rotations.front().time || channel->rotations.size() == 1)
                        {
                            rotation = channel->rotations.front().value;
                        }
                        else if (sample_time >= channel->rotations.back().time)
                        {
                            rotation = channel->rotations.back().value;
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
                                    rotation = math::Slerp(key0.value, key1.value, t);
                                    break;
                                }
                            }
                        }
                    }

                    if (!channel->scales.empty())
                    {
                        if (sample_time <= channel->scales.front().time || channel->scales.size() == 1)
                        {
                            scale = channel->scales.front().value;
                        }
                        else if (sample_time >= channel->scales.back().time)
                        {
                            scale = channel->scales.back().value;
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
                                    scale = math::Lerp(key0.value, key1.value, t);
                                    break;
                                }
                            }
                        }
                    }
                }

                const XMMATRIX local_matrix = XMMatrixAffineTransformation(XMLoadFloat3(&scale), XMVectorZero(), XMLoadFloat4(&rotation), XMLoadFloat3(&position));
                XMStoreFloat4x4(&local_matrices[bone_index], local_matrix);
            }

            for (Size bone_index = 0; bone_index < bone_count; ++bone_index)
            {
                const resource::Bone& bone = skeleton.bones[bone_index];
                XMMATRIX global_matrix = XMLoadFloat4x4(&local_matrices[bone_index]);
                if (bone.parent_index >= 0 && static_cast<Size>(bone.parent_index) < bone_index)
                {
                    global_matrix = global_matrix * XMLoadFloat4x4(&global_matrices[bone.parent_index]);
                }

                XMStoreFloat4x4(&global_matrices[bone_index], global_matrix);
                const XMMATRIX inverse_bind_matrix = XMLoadFloat4x4(&bone.inverse_bind_matrix);
                XMStoreFloat4x4(&animation.bone_matrices[bone_index], inverse_bind_matrix * global_matrix);

                const float4x4& bone_matrix = animation.bone_matrices[bone_index];
                const Size shader_bone_matrix_index = static_cast<Size>(current_bone_matrix_offset + bone_index) * 4;
                render_data.shader_bone_matrices[shader_bone_matrix_index + 0] = { bone_matrix._11, bone_matrix._21, bone_matrix._31, bone_matrix._41 };
                render_data.shader_bone_matrices[shader_bone_matrix_index + 1] = { bone_matrix._12, bone_matrix._22, bone_matrix._32, bone_matrix._42 };
                render_data.shader_bone_matrices[shader_bone_matrix_index + 2] = { bone_matrix._13, bone_matrix._23, bone_matrix._33, bone_matrix._43 };
                render_data.shader_bone_matrices[shader_bone_matrix_index + 3] = { bone_matrix._14, bone_matrix._24, bone_matrix._34, bone_matrix._44 };
            }

            animation.bone_matrices_dirty = false;
        });

        jobsystem::Wait(sub_ctx);

    }
}
