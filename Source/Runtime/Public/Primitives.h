#pragma once
#include "MathTypes.h"
#include "Types.h"
#include <array>
#include <cfloat>

namespace won::math
{
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

		// Gribb-Hartmann method : Frusutm Extraction from View-Projection Matrix
        void FromVPMatrix(const float4x4& vp)
        {
            auto make_plane = [](float nx, float ny, float nz, float d) -> Plane
            {
                const float len = std::sqrtf(nx * nx + ny * ny + nz * nz);
                if (len > 0.0f)
                {
                    return { { nx / len, ny / len, nz / len }, d / len };
                }
                return { { nx, ny, nz }, d };
            };

            planes[0] = make_plane(vp._14 + vp._11, vp._24 + vp._21, vp._34 + vp._31, vp._44 + vp._41); // Left
            planes[1] = make_plane(vp._14 - vp._11, vp._24 - vp._21, vp._34 - vp._31, vp._44 - vp._41); // Right
            planes[2] = make_plane(vp._14 + vp._12, vp._24 + vp._22, vp._34 + vp._32, vp._44 + vp._42); // Bottom
            planes[3] = make_plane(vp._14 - vp._12, vp._24 - vp._22, vp._34 - vp._32, vp._44 - vp._42); // Top
            planes[4] = make_plane(vp._13,          vp._23,          vp._33,          vp._43);          // Near
            planes[5] = make_plane(vp._14 - vp._13, vp._24 - vp._23, vp._34 - vp._33, vp._44 - vp._43); // Far
        }
    };

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

        inline float3 GetCenter() const
        {
            return float3{ (min.x + max.x) * 0.5f,
                (min.y + max.y) * 0.5f,
                (min.z + max.z) * 0.5f };
        }

        inline float3 GetExtent() const 
        {
            return float3{ (max.x - min.x) * 0.5f,
                (max.y - min.y) * 0.5f,
                (max.z - min.z) * 0.5f };
        }

        inline AABB TransformAABB(const XMMATRIX& transform) const
        {
            AABB transformed_aabb = {};
            transformed_aabb.Invalidate();
            if (!IsValid())
            {
                return transformed_aabb;
            }

            // affine transforms only: the extent is rotated by the absolute value of the linear part
            const float3 center = GetCenter();
            const float3 extent = GetExtent();

            const XMVECTOR transformed_center = XMVector3TransformCoord(XMLoadFloat3(&center), transform);
            const XMVECTOR transformed_extent = XMVectorAdd(
                XMVectorAdd(
                    XMVectorScale(XMVectorAbs(transform.r[0]), extent.x),
                    XMVectorScale(XMVectorAbs(transform.r[1]), extent.y)),
                XMVectorScale(XMVectorAbs(transform.r[2]), extent.z));

            XMStoreFloat3(&transformed_aabb.min, XMVectorSubtract(transformed_center, transformed_extent));
            XMStoreFloat3(&transformed_aabb.max, XMVectorAdd(transformed_center, transformed_extent));

            return transformed_aabb;
        }

        inline AABB TransformAABB(const float4x4& transform) const
        {
            const XMMATRIX world = XMLoadFloat4x4(&transform);
            return TransformAABB(world);
        }

        inline bool IntersectFrustum(const Frustum& frustum) const
        {
            for (const Plane& plane : frustum.planes)
            {
                const float3 p = {
                    plane.normal.x >= 0.0f ? max.x : min.x,
                    plane.normal.y >= 0.0f ? max.y : min.y,
                    plane.normal.z >= 0.0f ? max.z : min.z,
                };
                if (plane.normal.x * p.x + plane.normal.y * p.y + plane.normal.z * p.z + plane.distance < 0.0f)
                {
                    return false;
                }
            }
            return true;

        }

        inline bool IntersectAABB(const Ray& ray, float min_distance, float max_distance, float& out_distance) const
        {
            if (!IsValid())
            {
                return false;
            }

            float near_distance = min_distance;
            float far_distance = max_distance;
            auto get_axis_value = [](const float3& value, uint32 axis) { return axis == 0 ? value.x : (axis == 1 ? value.y : value.z); };

            for (uint32 axis = 0; axis < 3; ++axis)
            {
                const float origin = get_axis_value(ray.origin, axis);
                const float direction = get_axis_value(ray.direction, axis);
                const float bounds_min = get_axis_value(min, axis);
                const float bounds_max = get_axis_value(max, axis);

                if (std::abs(direction) < 0.000001f)
                {
                    if (origin < bounds_min || origin > bounds_max)
                    {
                        return false;
                    }
                    continue;
                }

                const float inv_direction = 1.0f / direction;
                float t0 = (bounds_min - origin) * inv_direction;
                float t1 = (bounds_max - origin) * inv_direction;
                if (t0 > t1)
                {
                    std::swap(t0, t1);
                }

                near_distance = (std::max)(near_distance, t0);
                far_distance = (std::min)(far_distance, t1);
                if (near_distance > far_distance)
                {
                    return false;
                }
            }

            out_distance = near_distance;
            return true;
        }
    };

    struct Sphere
    {
        float3 center = {};
        float radius = 0.0f;
    };
}
