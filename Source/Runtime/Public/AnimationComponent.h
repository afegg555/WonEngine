#pragma once
#include "Animation.h"
#include "Primitives.h"
#include "Types.h"

namespace won::ecs
{
    struct AnimationComponent
    {
        Vector<std::shared_ptr<resource::AnimationClip>> clips;
        uint32 current_clip_index = 0;
        float time = 0.0f;
        float speed = 1.0f;
        bool loop = true;
        bool playing = true;

        uint32 prev_clip_index = 0;
        float prev_time = 0.0f;
        float blend_duration = 0.0f;
        float blend_elapsed = 0.0f;
        bool blending = false;

        Vector<float4x4> bone_matrices;
        math::AABB skinned_local_bounds = { XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX), XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX) };
        uint32 bone_matrix_offset = 0;
        bool bone_matrices_dirty = true;

        float event_scan_time = 0.0f;
    };
}
