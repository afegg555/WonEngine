#pragma once
#include "Animation.h"
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

        Vector<float4x4> bone_matrices;
        uint32 bone_matrix_offset = 0;
        bool bone_matrices_dirty = true;
    };
}
