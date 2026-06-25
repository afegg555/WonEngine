#include "View.h"
#include "CameraComponent.h"
#include "MathUtils.h"
#include "JobSystem.h"
#include "Primitives.h"

#include <numeric>

namespace won::rendering
{
    void View::Update(float dt)
    {
        if (scene)
        {
            scene->Update(dt);
            BuildSortedIndices();
        }
    }

    bool View::RayCast(float2 screen_position, ecs::RayCastHit& out_hit, bool use_local_bvh, uint32 layer_mask) const
    {
        out_hit = {};
        if (!scene || viewport.width <= 0 || viewport.height <= 0)
        {
            return false;
        }

        ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(camera_entity);
        if (!camera)
        {
            return false;
        }

        const float viewport_x = static_cast<float>(viewport.x);
        const float viewport_y = static_cast<float>(viewport.y);
        const float viewport_width = static_cast<float>(viewport.width);
        const float viewport_height = static_cast<float>(viewport.height);
        if (screen_position.x < viewport_x || screen_position.y < viewport_y ||
            screen_position.x > viewport_x + viewport_width || screen_position.y > viewport_y + viewport_height)
        {
            return false;
        }

        const float viewport_u = (screen_position.x - viewport_x) / viewport_width;
        const float viewport_v = (screen_position.y - viewport_y) / viewport_height;
        const float ndc_x = viewport_u * 2.0f - 1.0f;
        const float ndc_y = 1.0f - viewport_v * 2.0f;
        const XMMATRIX inv_view_projection = XMLoadFloat4x4(&camera->inv_view_projection);
        const XMVECTOR near_position = XMVector3TransformCoord(XMVectorSet(ndc_x, ndc_y, 1.0f, 1.0f), inv_view_projection);
        const XMVECTOR far_position = XMVector3TransformCoord(XMVectorSet(ndc_x, ndc_y, 0.0f, 1.0f), inv_view_projection);

        math::Ray ray = {};
        if (camera->IsOrtho())
        {
            XMStoreFloat3(&ray.origin, near_position);
            XMStoreFloat3(&ray.direction, XMVector3Normalize(far_position - near_position));
        }
        else
        {
            ray.origin = camera->eye;
            XMStoreFloat3(&ray.direction, XMVector3Normalize(far_position - XMLoadFloat3(&camera->eye)));
        }

        ecs::RayCastBVHHit bvh_hit = {};
        if (!scene->RayCastBVH(ray, bvh_hit, use_local_bvh, layer_mask))
        {
            return false;
        }

        out_hit = bvh_hit.hit;
        return true;
    }

    void View::BuildSortedIndices()
    {
        if (!scene || camera_entity == ecs::INVALID_ENTITY)
            return;

        const ecs::Scene::RenderData& render_data = scene->GetRenderData();
        const ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(camera_entity);
        const float3 eye = camera ? camera->eye : float3{};
        const math::Frustum* frustum = (options.enable_frustum_culling && camera) ? &camera->frustum : nullptr;
        const uint32 culling_mask = camera ? camera->culling_mask : 0xFFFFFFFF;
        // No iota fast path: layer_mask == 0 must be culled even when culling_mask is all-ones,
        // so every renderable needs a per-element layer test regardless of frustum presence.

        jobsystem::Context ctx;

        jobsystem::Execute(ctx, [&](jobsystem::JobArgs)
        {
            const auto& renderables = render_data.opaque_renderables;
            sorted_opaque_indices.clear();
            for (uint32 i = 0; i < static_cast<uint32>(renderables.size()); ++i)
            {
                const auto& r = renderables[i];
                if ((culling_mask & r.layer_mask) == 0)
                    continue;
                if (frustum && r.aabb.IsValid() && !r.aabb.IntersectFrustum(*frustum))
                    continue;
                sorted_opaque_indices.push_back(i);
            }
            std::sort(sorted_opaque_indices.begin(), sorted_opaque_indices.end(),
                [&](uint32 a, uint32 b)
                {
                    const auto& ra = renderables[a];
                    const auto& rb = renderables[b];
                    if (ra.push_constants.geometry_index != rb.push_constants.geometry_index)
                        return ra.push_constants.geometry_index < rb.push_constants.geometry_index;
                    if (ra.push_constants.material_index != rb.push_constants.material_index)
                        return ra.push_constants.material_index < rb.push_constants.material_index;
                    if (ra.shader_type != rb.shader_type)
                        return ra.shader_type < rb.shader_type;
                    if (ra.IsDoubleSided() != rb.IsDoubleSided())
                        return ra.IsDoubleSided() < rb.IsDoubleSided();
                    return ra.primitive_topology < rb.primitive_topology;
                });
        });

        jobsystem::Execute(ctx, [&](jobsystem::JobArgs)
        {
            const auto& renderables = render_data.transparent_renderables;
            sorted_transparent_indices.clear();
            for (uint32 i = 0; i < static_cast<uint32>(renderables.size()); ++i)
            {
                const auto& r = renderables[i];
                if ((culling_mask & r.layer_mask) == 0)
                    continue;
                if (frustum && r.aabb.IsValid() && !r.aabb.IntersectFrustum(*frustum))
                    continue;
                sorted_transparent_indices.push_back(i);
            }
            std::sort(sorted_transparent_indices.begin(), sorted_transparent_indices.end(),
                [&](uint32 a, uint32 b)
                {
                    return math::DistanceSquared(renderables[a].world_position, eye) >
                           math::DistanceSquared(renderables[b].world_position, eye);
                });
        });

        jobsystem::Execute(ctx, [&](jobsystem::JobArgs)
        {
            const auto& renderables = render_data.sprite_3d_renderables;
            sorted_sprite_3d_indices.clear();
            for (uint32 i = 0; i < static_cast<uint32>(renderables.size()); ++i)
            {
                const auto& r = renderables[i];
                if ((culling_mask & r.layer_mask) == 0)
                    continue;
                if (frustum && r.aabb.IsValid() && !r.aabb.IntersectFrustum(*frustum))
                    continue;
                sorted_sprite_3d_indices.push_back(i);
            }
            std::sort(sorted_sprite_3d_indices.begin(), sorted_sprite_3d_indices.end(),
                [&](uint32 a, uint32 b)
                {
                    return math::DistanceSquared(renderables[a].world_position, eye) >
                           math::DistanceSquared(renderables[b].world_position, eye);
                });
        });

        jobsystem::Execute(ctx, [&](jobsystem::JobArgs)
        {
            const auto& renderables = render_data.sprite_2d_renderables;
            if (!options.enable_viewport_culling)
            {
                sorted_sprite_2d_indices.resize(renderables.size());
                std::iota(sorted_sprite_2d_indices.begin(), sorted_sprite_2d_indices.end(), 0u);
            }
            else
            {
                const float vp_w = static_cast<float>(viewport.width);
                const float vp_h = static_cast<float>(viewport.height);
                sorted_sprite_2d_indices.clear();
                for (uint32 i = 0; i < static_cast<uint32>(renderables.size()); ++i)
                {
                    const auto& r = renderables[i];
                    const float px = r.anchor.x * vp_w + r.position.x;
                    const float py = r.anchor.y * vp_h + r.position.y;
                    const float l = px - r.pivot.x * r.size.x;
                    const float t = py - r.pivot.y * r.size.y;
                    if (l > vp_w || l + r.size.x < 0.0f || t > vp_h || t + r.size.y < 0.0f)
                        continue;
                    sorted_sprite_2d_indices.push_back(i);
                }
            }
            std::stable_sort(sorted_sprite_2d_indices.begin(), sorted_sprite_2d_indices.end(),
                [&](uint32 a, uint32 b)
                {
                    return renderables[a].layer < renderables[b].layer;
                });
        });

        jobsystem::Wait(ctx);
    }
}
