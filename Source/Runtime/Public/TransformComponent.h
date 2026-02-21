#pragma once
#include "MathUtils.h"
#include "Types.h"

namespace won::ecs
{
    struct TransformComponent
    {
        enum FLAGS
        {
            EMPTY = 0,
            DIRTY = 1 << 0,
        };

        uint flags = DIRTY;

        // local transform
        float3 position = {};
        float4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        float3 scale = { 1.0f, 1.0f, 1.0f };

        float4x4 world_transform = math::IDENTITY_MATRIX;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= DIRTY; } else { flags &= ~DIRTY; } }
        constexpr bool IsDirty() const { return flags & DIRTY; }

        void UpdateTransform()
        {
            if (IsDirty())
            {
                SetDirty(false);

                XMStoreFloat4x4(&world_transform, GetLocalTransform());
            }
        }
        XMMATRIX GetLocalTransform()
        {
            XMVECTOR xscale = XMVectorSet(scale.x, scale.y, scale.z, 1.0f);
            XMVECTOR xrotation = XMLoadFloat4(&rotation);
            XMVECTOR xtranslation = XMVectorSet(position.x, position.y, position.z, 1.0f);
            return XMMatrixAffineTransformation(xscale, XMVectorZero(), xrotation, xtranslation);
        }
        XMMATRIX GetWorldTransform() const { return XMLoadFloat4x4(&world_transform); };
    };
}
