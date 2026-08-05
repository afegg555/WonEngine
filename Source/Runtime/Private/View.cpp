#include "View.h"
#include "GPUScene.h"
#include "CameraComponent.h"
#include "Input.h"
#include "MathUtils.h"
#include "JobSystem.h"
#include "Primitives.h"

#include <cmath>
#include <numeric>

namespace won::rendering
{
    bool View::HasPointerFocus() const
    {
        const float2 p = io::GetMouseState().position;
        return p.x >= viewport.x && p.x < viewport.x + viewport.width &&
               p.y >= viewport.y && p.y < viewport.y + viewport.height;
    }

    ecs::Entity View::HitTestUI(float2 pointer) const
    {
        const float2 vp = { static_cast<float>(viewport.width), static_cast<float>(viewport.height) };
        const float2 local = { pointer.x - viewport.x, pointer.y - viewport.y };
        auto buttons = scene->GetComponentArray<ecs::ButtonComponent>().get();
        auto rects = scene->GetComponentArray<ecs::RectTransform2DComponent>().get();
        auto sprites = scene->GetComponentArray<ecs::Sprite2DComponent>().get();
        auto texts = scene->GetComponentArray<ecs::Text2DComponent>().get();
        auto materials = scene->GetComponentArray<ecs::MaterialComponent>().get();
        if (!buttons || !rects)
        {
            return ecs::INVALID_ENTITY;
        }

        ecs::Entity best = ecs::INVALID_ENTITY;
        int32 best_layer = 0;
        // TODO: needs job system parallelization if the number of buttons is large
		for (Size i = 0; i < buttons->GetSize(); ++i)
        {
            if (!buttons->data[i].enabled)
            {
                continue;
            }
            const ecs::Entity e = buttons->index_to_entity[i];
            if (!rects->HasData(e))
            {
                continue;
            }
            const ecs::RectTransform2DComponent& rect = rects->GetData(e);
            if ((rect.layer_mask & ui_layer_mask) == 0)
            {
                continue;
            }

            const bool has_sprite = sprites && sprites->HasData(e);
            const bool has_text = texts && texts->HasData(e);
            if (!has_sprite && !has_text)
            {
                continue;
            }
            if (!materials || !materials->HasData(e) || materials->GetData(e).GetMaterialSlotCount() == 0)
            {
                continue;
            }

            float2 scale = { 1.0f, 1.0f };
            if (rect.reference_resolution.x > 0.0f && rect.reference_resolution.y > 0.0f)
            {
                const float s = std::pow(vp.x / rect.reference_resolution.x, 1.0f - rect.match) * std::pow(vp.y / rect.reference_resolution.y, rect.match);
                scale = { s, s };
            }
            const float2 mn = { rect.resolved_position.x * scale.x, rect.resolved_position.y * scale.y };
            const float2 sz = { rect.resolved_size.x * scale.x, rect.resolved_size.y * scale.y };
            if (local.x < mn.x || local.x > mn.x + sz.x || local.y < mn.y || local.y > mn.y + sz.y)
            {
                continue;
            }

            const int32 layer = has_sprite ? sprites->GetData(e).layer : texts->GetData(e).layer;
            if (best == ecs::INVALID_ENTITY || layer >= best_layer)
            {
                best = e;
                best_layer = layer;
            }
        }
        return best;
    }

    void View::UpdateUIInteraction()
    {
        if (!scene || !HasPointerFocus())
        {
            ui_hovered = ecs::INVALID_ENTITY;
            ui_press_target = ecs::INVALID_ENTITY;
            return;
        }
        const float2 pointer = io::GetMouseState().position;
        ui_hovered = HitTestUI(pointer);
        if (io::IsPressed(io::MOUSE_BUTTON_LEFT))
        {
            ui_press_target = ui_hovered;
        }
        if (io::IsReleased(io::MOUSE_BUTTON_LEFT))
        {
            if (ui_hovered != ecs::INVALID_ENTITY && ui_hovered == ui_press_target)
            {
                scene->QueueUIClick(ui_hovered);
            }
            ui_press_target = ecs::INVALID_ENTITY;
        }
    }

    bool View::ScreenToRay(float2 screen_position, math::Ray& out_ray) const
    {
        out_ray = {};
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

        if (camera->IsOrtho())
        {
            XMStoreFloat3(&out_ray.origin, near_position);
            XMStoreFloat3(&out_ray.direction, XMVector3Normalize(far_position - near_position));
        }
        else
        {
            out_ray.origin = camera->eye;
            XMStoreFloat3(&out_ray.direction, XMVector3Normalize(far_position - XMLoadFloat3(&camera->eye)));
        }
        return true;
    }

    bool View::RayCast(float2 screen_position, ecs::RayCastHit& out_hit, bool use_local_bvh, uint32 layer_mask) const
    {
        out_hit = {};
        math::Ray ray = {};
        if (!ScreenToRay(screen_position, ray))
        {
            return false;
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

        GPUScene& gpu_scene = scene->GetGPUScene();
        const ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(camera_entity);
        const float3 eye = camera ? camera->eye : float3{};
        const math::Frustum* frustum = nullptr;
        if (options.enable_frustum_culling && camera)
        {
            if (freeze_culling)
            {
                if (!frozen_frustum_valid)
                {
                    frozen_frustum = camera->frustum;
                    frozen_frustum_valid = true;
                }
                frustum = &frozen_frustum;
            }
            else
            {
                frozen_frustum_valid = false;
                frustum = &camera->frustum;
            }
        }
        const uint32 culling_mask = camera ? camera->culling_mask : 0xFFFFFFFF;
        // No iota fast path: layer_mask == 0 must be culled even when culling_mask is all-ones,
        // so every renderable needs a per-element layer test regardless of frustum presence.

        jobsystem::Context ctx;

        jobsystem::Execute(ctx, [&](jobsystem::JobArgs)
        {
            const auto& renderables = gpu_scene.opaque_renderables;
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
            const auto& renderables = gpu_scene.transparent_renderables;
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
            const auto& renderables = gpu_scene.sprite_3d_renderables;
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
            const auto& renderables = gpu_scene.sprite_2d_renderables;
            const float vp_w = static_cast<float>(viewport.width);
            const float vp_h = static_cast<float>(viewport.height);
            sorted_sprite_2d_indices.clear();
            for (uint32 i = 0; i < static_cast<uint32>(renderables.size()); ++i)
            {
                const auto& r = renderables[i];
                if ((r.layer_mask & ui_layer_mask) == 0)
                {
                    continue;
                }
                if (options.enable_viewport_culling)
                {
                    float s = 1.0f;
                    if (r.reference_resolution.x > 0.0f && r.reference_resolution.y > 0.0f)
                    {
                        s = std::pow(vp_w / r.reference_resolution.x, 1.0f - r.match) * std::pow(vp_h / r.reference_resolution.y, r.match);
                    }
                    const float sw = r.size.x * s;
                    const float sh = r.size.y * s;
                    const float px = r.anchor.x * vp_w + r.position.x * s;
                    const float py = r.anchor.y * vp_h + r.position.y * s;
                    const float l = px - r.pivot.x * sw;
                    const float t = py - r.pivot.y * sh;
                    if (l > vp_w || l + sw < 0.0f || t > vp_h || t + sh < 0.0f)
                    {
                        continue;
                    }
                }
                sorted_sprite_2d_indices.push_back(i);
            }
            std::stable_sort(sorted_sprite_2d_indices.begin(), sorted_sprite_2d_indices.end(),
                [&](uint32 a, uint32 b)
                {
                    return renderables[a].layer < renderables[b].layer;
                });
        });

        jobsystem::Wait(ctx);
    }

    ecs::Entity View::ResolveCamera() const
    {
        if (manual_camera || !scene)
        {
            return camera_entity;
        }

        if (auto sequence_array = scene->GetComponentArray<ecs::SequenceComponent>())
        {
            for (Size i = 0; i < sequence_array->GetSize(); ++i)
            {
                const ecs::Entity cut_camera = sequence_array->data[i].cut_camera;
                if (cut_camera == ecs::INVALID_ENTITY)
                {
                    continue;
                }
                const ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(cut_camera);
                if (camera && camera->viewer_index == viewer_index)
                {
                    return cut_camera;
                }
            }
        }

        for (ecs::Entity entity : scene->GetEntities())
        {
            const ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(entity);
            if (camera && camera->IsActive() && camera->viewer_index == viewer_index)
            {
                return entity;
            }
        }
        return ecs::INVALID_ENTITY;
    }
}
