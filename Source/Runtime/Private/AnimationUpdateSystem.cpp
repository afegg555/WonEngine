#include "AnimationUpdateSystem.h"

#include "Animation.h"
#include "JobSystem.h"
#include "MathUtils.h"
#include "Mesh.h"
#include "Scene.h"

#include <algorithm>
#include <atomic>

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

        jobsystem::Dispatch(sub_ctx, (uint32)animation_array->GetSize(), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args) {
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
                animation.event_scan_time = animation.time;
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

        struct AnimationEventBucket
        {
            Vector<std::pair<Entity, String>> events;
        };
        const bool should_dispatch_events = dispatch_events;
        Vector<AnimationEventBucket> event_buckets(should_dispatch_events ? jobsystem::DispatchGroupCount((uint32)animation_array->GetSize(), jobsystem::groupsize_heavy) : 0);

        jobsystem::Dispatch(sub_ctx, (uint32)animation_array->GetSize(), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args) {
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
                animation.event_scan_time = animation.time;
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
            const float duration_seconds = clip->DurationSeconds();
            if (animation.playing)
            {
                const float advanced_time = animation.time + delta_time * animation.speed;
                const float new_time = animation.loop ? math::Wrap(advanced_time, duration_seconds) : math::Clamp(advanced_time, 0.0f, duration_seconds);

                if (should_dispatch_events && animation.speed > 0.0f && !clip->events.empty())
                {
                    Vector<std::pair<Entity, String>>& event_bucket = event_buckets[args.group_id].events;
                    const float scan_from = animation.event_scan_time;
                    auto emit_events = [&](float lo, float hi, bool inclusive_lo)
                    {
                        for (const resource::AnimationEventMarker& marker : clip->events)
                        {
                            const bool lo_ok = inclusive_lo ? (marker.time_seconds >= lo) : (marker.time_seconds > lo);
                            if (lo_ok && marker.time_seconds <= hi)
                            {
                                event_bucket.emplace_back(entity, marker.name);
                            }
                        }
                    };
                    if (new_time < scan_from)
                    {
                        emit_events(scan_from, duration_seconds, false);
                        emit_events(0.0f, new_time, true);
                    }
                    else if (scan_from <= 0.0f)
                    {
                        emit_events(0.0f, new_time, true);
                    }
                    else
                    {
                        emit_events(scan_from, new_time, false);
                    }
                }

                animation.time = new_time;
                animation.event_scan_time = new_time;
                animation.bone_matrices_dirty = true;
            }
            else
            {
                animation.event_scan_time = animation.time;
            }

            resource::Skeleton& skeleton = *geometry.mesh->skeleton;
            const Size bone_count = skeleton.bones.size();
            if (bone_count == 0)
            {
                animation.bone_matrices.clear();
                animation.bone_matrix_offset = 0;
                return;
            }

            const uint32 current_bone_matrix_offset = animation.bone_matrix_offset;

            float blend_weight = 1.0f;
            const resource::AnimationClip* prev_clip = nullptr;
            if (animation.blending)
            {
                blend_weight = animation.blend_duration > 0.0f ? std::clamp(animation.blend_elapsed / animation.blend_duration, 0.0f, 1.0f) : 1.0f;
                if (animation.prev_clip_index < animation.clips.size() && animation.clips[animation.prev_clip_index])
                {
                    prev_clip = animation.clips[animation.prev_clip_index].get();
                }
                if (animation.playing)
                {
                    animation.blend_elapsed += delta_time;
                    if (prev_clip && prev_clip->IsValid())
                    {
                        //const float prev_ticks_per_second = prev_clip->ticks_per_second > 0.0f ? prev_clip->ticks_per_second : 1.0f;
                        const float prev_duration_seconds = prev_clip->DurationSeconds();
                        const float advanced_prev_time = animation.prev_time + delta_time * animation.speed;
                        animation.prev_time = animation.loop ? math::Wrap(advanced_prev_time, prev_duration_seconds) : math::Clamp(advanced_prev_time, 0.0f, prev_duration_seconds);
                    }
                }
                if (blend_weight >= 1.0f || !prev_clip || !prev_clip->IsValid())
                {
                    animation.blending = false;
                    blend_weight = 1.0f;
                    prev_clip = nullptr;
                }
            }

            Vector<resource::BonePose> current_pose;
            resource::SampleAnimationPose(*clip, animation.time * ticks_per_second, skeleton, current_pose);

            const Vector<resource::BonePose>* final_pose = &current_pose;
            Vector<resource::BonePose> blended_pose;
            if (prev_clip)
            {
                const float prev_ticks_per_second = prev_clip->ticks_per_second > 0.0f ? prev_clip->ticks_per_second : 1.0f;
                Vector<resource::BonePose> prev_pose;
                resource::SampleAnimationPose(*prev_clip, animation.prev_time * prev_ticks_per_second, skeleton, prev_pose);
                blended_pose.resize(bone_count);
                for (Size bone_index = 0; bone_index < bone_count; ++bone_index)
                {
                    blended_pose[bone_index].position = math::Lerp(prev_pose[bone_index].position, current_pose[bone_index].position, blend_weight);
                    blended_pose[bone_index].scale = math::Lerp(prev_pose[bone_index].scale, current_pose[bone_index].scale, blend_weight);
                    blended_pose[bone_index].rotation = math::Slerp(prev_pose[bone_index].rotation, current_pose[bone_index].rotation, blend_weight);
                }
                final_pose = &blended_pose;
            }

            Vector<float4x4> local_matrices;
            Vector<float4x4> global_matrices;
            local_matrices.resize(bone_count, math::IDENTITY_MATRIX);
            global_matrices.resize(bone_count, math::IDENTITY_MATRIX);
            animation.bone_matrices.resize(bone_count, math::IDENTITY_MATRIX);

            for (Size bone_index = 0; bone_index < bone_count; ++bone_index)
            {
                const resource::BonePose& pose = (*final_pose)[bone_index];
                const XMMATRIX local_matrix = XMMatrixAffineTransformation(XMLoadFloat3(&pose.scale), XMVectorZero(), XMLoadFloat4(&pose.rotation), XMLoadFloat3(&pose.position));
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

        for (AnimationEventBucket& event_bucket : event_buckets)
        {
            for (std::pair<Entity, String>& animation_event : event_bucket.events)
            {
                scene.QueueAnimationEvent(animation_event.first, animation_event.second);
            }
        }
    }
}
