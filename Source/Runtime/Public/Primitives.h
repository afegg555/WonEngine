#pragma once
#include "MathTypes.h"
#include <array>
#include <cfloat>

namespace won::math
{
    struct AABB
    {
        float3 min = {};
        float3 max = {};

        void Invalidate()
        {
            min = XMFLOAT3(FLT_MAX, FLT_MAX, FLT_MAX);
            max = XMFLOAT3(-FLT_MAX, -FLT_MAX, -FLT_MAX);
        }

        bool IsValid() const
        {
            return min.x <= max.x && min.y <= max.y && min.z <= max.z;
        }

        void CreateFromHalfWidth(const float3& center, const float3& halfwidth)
        {
            min = XMFLOAT3(center.x - halfwidth.x, center.y - halfwidth.y, center.z - halfwidth.z);
            max = XMFLOAT3(center.x + halfwidth.x, center.y + halfwidth.y, center.z + halfwidth.z);
        }

        void Merge(const AABB& other)
        {
            if (!other.IsValid())
            {
                return;
            }

            if (!IsValid())
            {
                *this = other;
                return;
            }

            min.x = (std::min)(min.x, other.min.x);
            min.y = (std::min)(min.y, other.min.y);
            min.z = (std::min)(min.z, other.min.z);
            max.x = (std::max)(max.x, other.max.x);
            max.y = (std::max)(max.y, other.max.y);
            max.z = (std::max)(max.z, other.max.z);
        }
    };

    inline AABB TransformAABB(const AABB& aabb, const float4x4& transform)
    {
        AABB transformed_aabb = {};
        transformed_aabb.Invalidate();
        if (!aabb.IsValid())
        {
            return transformed_aabb;
        }

        const XMMATRIX world = XMLoadFloat4x4(&transform);
        const float3 corners[8] = {
            { aabb.min.x, aabb.min.y, aabb.min.z },
            { aabb.max.x, aabb.min.y, aabb.min.z },
            { aabb.min.x, aabb.max.y, aabb.min.z },
            { aabb.max.x, aabb.max.y, aabb.min.z },
            { aabb.min.x, aabb.min.y, aabb.max.z },
            { aabb.max.x, aabb.min.y, aabb.max.z },
            { aabb.min.x, aabb.max.y, aabb.max.z },
            { aabb.max.x, aabb.max.y, aabb.max.z }
        };

        for (const float3& corner : corners)
        {
            const XMVECTOR world_corner = XMVector3TransformCoord(XMLoadFloat3(&corner), world);
            float3 transformed_corner = {};
            XMStoreFloat3(&transformed_corner, world_corner);

            if (!transformed_aabb.IsValid())
            {
                transformed_aabb.min = transformed_corner;
                transformed_aabb.max = transformed_corner;
                continue;
            }

            transformed_aabb.min.x = (std::min)(transformed_aabb.min.x, transformed_corner.x);
            transformed_aabb.min.y = (std::min)(transformed_aabb.min.y, transformed_corner.y);
            transformed_aabb.min.z = (std::min)(transformed_aabb.min.z, transformed_corner.z);
            transformed_aabb.max.x = (std::max)(transformed_aabb.max.x, transformed_corner.x);
            transformed_aabb.max.y = (std::max)(transformed_aabb.max.y, transformed_corner.y);
            transformed_aabb.max.z = (std::max)(transformed_aabb.max.z, transformed_corner.z);
        }

        return transformed_aabb;
    }

    struct Ray
    {
        float3 origin = {};
        float3 direction = { 0.0f, 0.0f, 1.0f };
    };

    struct Plane
    {
        float3 normal = { 0.0f, 1.0f, 0.0f };
        float distance = 0.0f;
    };

    struct Frustum
    {
        std::array<Plane, 6> planes = {};
    };

    struct Sphere
    {
        float3 center = {};
        float radius = 0.0f;
    };
}
