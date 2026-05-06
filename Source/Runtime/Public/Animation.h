#pragma once
#include "MathUtils.h"
#include "Resource.h"
#include "Types.h"

namespace won::resource
{
    inline constexpr uint32 INVALID_BONE_INDEX = ~0u;

    template <typename T>
    struct AnimationKeyframe
    {
        float time = 0.0f;
        T value = {};
    };

    struct Bone
    {
        String name;
        int32 parent_index = -1;
        float4x4 inverse_bind_matrix = math::IDENTITY_MATRIX; // from mesh space to bone space in bind pose
        float4x4 bind_local_transform = math::IDENTITY_MATRIX; // local transform in bind pose
    };

    struct Skeleton : public Resource
    {
        Vector<Bone> bones;
        UnorderedMap<String, uint32> bone_name_to_index;

        bool IsValid() const override
        {
            return !bones.empty();
        }
    };

    struct AnimationChannel
    {
        uint32 bone_index = INVALID_BONE_INDEX;
        Vector<AnimationKeyframe<float3>> positions;
        Vector<AnimationKeyframe<float4>> rotations;
        Vector<AnimationKeyframe<float3>> scales;

        bool IsValid() const
        {
            return bone_index != INVALID_BONE_INDEX && (!positions.empty() || !rotations.empty() || !scales.empty());
        }
    };

    struct AnimationClip : public Resource
    {
        String name;
        float duration = 0.0f;
        float ticks_per_second = 1.0f;
        Vector<AnimationChannel> channels;

        bool IsValid() const override
        {
            return duration > 0.0f && !channels.empty();
        }
    };
}
