#pragma once
#include "MathUtils.h"
#include "Types.h"

namespace won::ecs
{
    struct TransformComponent
    {
        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
        };

        uint flags = Dirty;

        // local transform
        float3 position = {};
        float4 rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        float3 scale = { 1.0f, 1.0f, 1.0f };

        float4x4 world_transform = math::IDENTITY_MATRIX;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return flags & Dirty; }

        void Translate(const float3& value)
        {
            SetDirty();
            position.x += value.x;
            position.y += value.y;
            position.z += value.z;
        }
        void RotateRollPitchYaw(const float3& value)
        {
            SetDirty();

            XMVECTOR quat = XMLoadFloat4(&rotation);
            XMVECTOR x = XMQuaternionRotationRollPitchYaw(value.x, 0, 0);
            XMVECTOR y = XMQuaternionRotationRollPitchYaw(0, value.y, 0);
            XMVECTOR z = XMQuaternionRotationRollPitchYaw(0, 0, value.z);

            quat = XMQuaternionMultiply(x, quat);
            quat = XMQuaternionMultiply(quat, y);
            quat = XMQuaternionMultiply(z, quat);
            quat = XMQuaternionNormalize(quat);

            XMStoreFloat4(&rotation, quat);
        }
        void Scale(const float3& value)
        {
            SetDirty();
            scale.x *= value.x;
            scale.y *= value.y;
            scale.z *= value.z;
        }
        void MatrixTransform(const float4x4& matrix)
        {
            SetDirty();

            XMMATRIX xmat = XMLoadFloat4x4(&matrix);
            
            XMVECTOR S;
            XMVECTOR R;
            XMVECTOR T;
            XMMatrixDecompose(&S, &R, &T, GetLocalTransform() * xmat);

            XMStoreFloat3(&scale, S);
            XMStoreFloat4(&rotation, R);
            XMStoreFloat3(&position, T);
        }
        void ClearTransform()
        {
            SetDirty();
            scale = XMFLOAT3(1, 1, 1);
            rotation = XMFLOAT4(0, 0, 0, 1);
            position = XMFLOAT3(0, 0, 0);
        }
        
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
