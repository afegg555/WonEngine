#include "View.h"
#include "GPUScene.h"
#include "CameraComponent.h"
#include "LightComponent.h"
#include "RectPacker.h"
#include "Backlog.h"
#include "Profiler.h"
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

    void View::Update(float delta_time, uint64 update_index, bool simulation_paused)
    {
        if (!scene)
        {
            return;
        }

        UpdateUIInteraction();

        if (scene->GetUpdateIndex() != update_index)
        {
            scene->SetUpdateIndex(update_index);
            if (!simulation_paused)
            {
                scene->Update(delta_time);
            }
        }

        camera_entity = ResolveCamera();

        BuildShadowSlices();
        {
            auto sorted_range = profiler::ScopedRangeCPU("Build Sorted Indices");
            BuildSortedIndices();
            BuildForwardLightList();
        }
    }

    void View::BuildShadowSlices()
    {
        shadow_resources.shader_shadow_cascades.clear();
        shadow_resources.render_shadow_slices.clear();
        shadow_resources.shadow_map_atlas_size = { 0, 0 };

        const ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(camera_entity);
        auto light_array = scene->GetComponentArray<ecs::LightComponent>().get();
        rendering::GPUScene& gpu_scene = scene->GetGPUScene();
        if ((show_flags & Show_Shadows) != 0 && camera)
        {
            const uint32 total_light_count = light_array ? static_cast<uint32>(light_array->GetSize()) : 0u;
            shadow_resources.light_shadow_slices.assign(gpu_scene.shader_lights.size(), 0u);
            rectpacker::State atlas_packer = {};
            uint32 packed_directional_index = 0;

            ecs::LightComponent derived_sun_light = {};
            if (gpu_scene.has_derived_sun)
            {
                derived_sun_light.flags = ecs::LightComponent::Active | ecs::LightComponent::Dynamic;
                if (gpu_scene.direct_sun_shadow.cast_shadow)
                {
                    derived_sun_light.flags |= ecs::LightComponent::CastShadow;
                }
                derived_sun_light.type = ecs::LightComponent::LightType::Directional;
                derived_sun_light.direction = {
                    -gpu_scene.shader_environment.sun_direction.x,
                    -gpu_scene.shader_environment.sun_direction.y,
                    -gpu_scene.shader_environment.sun_direction.z
                };
                derived_sun_light.shadow_map_resolution = gpu_scene.direct_sun_shadow.shadow_resolution;
                derived_sun_light.shadow_cascade_count = gpu_scene.direct_sun_shadow.cascade_count;
                derived_sun_light.shadow_cascade_lambda = gpu_scene.direct_sun_shadow.cascade_lambda;
                derived_sun_light.shadow_cascade_blend = gpu_scene.direct_sun_shadow.cascade_blend;
                derived_sun_light.shadow_distance = gpu_scene.direct_sun_shadow.shadow_distance;
            }

            for (uint32 light_index = 0u; light_index <= total_light_count; ++light_index)
            {
                const bool is_derived_sun = light_index == total_light_count;
                if (is_derived_sun && !gpu_scene.has_derived_sun)
                {
                    break;
                }

                const ecs::LightComponent& light = is_derived_sun ? derived_sun_light : light_array->data[light_index];

                if (!light.IsActive() || light.type != ecs::LightComponent::LightType::Directional)
                {
                    continue;
                }

                const uint32 slice_index = is_derived_sun ? gpu_scene.derived_sun_index : packed_directional_index++;

                if (!light.IsDynamic() || !light.IsCastShadow())
                {
                    continue;
                }

                const uint32 cascade_count = camera->IsOrtho() ? 1u : (std::min)(light.shadow_cascade_count, SHADOW_CASCADE_COUNT_MAX);
                if (cascade_count == 0)
                {
                    continue;
                }

                const uint32 cascade_offset = static_cast<uint32>(shadow_resources.shader_shadow_cascades.size());
                shadow_resources.light_shadow_slices[slice_index] = (cascade_offset & 0xFFFFu) | ((cascade_count & 0xFFFFu) << 16u);

                float shadow_far = camera->far_plane;
                if (light.shadow_distance > 0.0f)
                {
                    shadow_far = math::Clamp(light.shadow_distance, camera->near_plane, camera->far_plane);
                }

                float split_distances[SHADOW_CASCADE_COUNT_MAX + 1] = {};
                split_distances[0] = camera->near_plane;
                for (uint32 cascade_index = 1; cascade_index <= cascade_count; ++cascade_index)
                {
                    const float t = static_cast<float>(cascade_index) / static_cast<float>(cascade_count);
                    const float uniform_split = math::Lerp(camera->near_plane, shadow_far, t);
                    const float log_split = camera->near_plane * std::pow(shadow_far / camera->near_plane, t);
                    split_distances[cascade_index] = math::Lerp(uniform_split, log_split, light.shadow_cascade_lambda);
                }
				split_distances[cascade_count] = shadow_far; // to avoid floating point precision issue

                XMVECTOR light_direction = XMVector3Normalize(XMLoadFloat3(&light.direction));
                XMVECTOR light_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                if (std::abs(XMVectorGetX(XMVector3Dot(light_up, light_direction))) > 0.99f)
                {
                    light_up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                }

                for (uint32 cascade_index = 0; cascade_index < cascade_count; ++cascade_index)
                {
                    const float split_near = split_distances[cascade_index];
                    const float split_far = split_distances[cascade_index + 1];
                    std::array<float3, 8> frustum_corners = {};
                    const float near_to_far = camera->far_plane - camera->near_plane;
                    const float split_t_near = std::abs(near_to_far) > 0.0001f ? (split_near - camera->near_plane) / near_to_far : 0.0f;
                    const float split_t_far = std::abs(near_to_far) > 0.0001f ? (split_far - camera->near_plane) / near_to_far : 1.0f;

                    for (uint32 corner_index = 0; corner_index < 4; ++corner_index)
                    {
                        const float3& full_near_corner = {
                            camera->corners_np[corner_index].x,
                            camera->corners_np[corner_index].y,
                            camera->corners_np[corner_index].z
                        };
                        const float3& full_far_corner = {
                            camera->corners_fp[corner_index].x,
                            camera->corners_fp[corner_index].y,
                            camera->corners_fp[corner_index].z
                        };

                        frustum_corners[corner_index] = math::Lerp(full_near_corner, full_far_corner, split_t_near);
                        frustum_corners[corner_index + 4] = math::Lerp(full_near_corner, full_far_corner, split_t_far);
                    }

                    float3 frustum_center = {};
                    for (const float3& corner : frustum_corners)
                    {
                        frustum_center.x += corner.x;
                        frustum_center.y += corner.y;
                        frustum_center.z += corner.z;
                    }
                    frustum_center.x /= 8.0f;
                    frustum_center.y /= 8.0f;
                    frustum_center.z /= 8.0f;

                    const XMVECTOR xcenter = XMLoadFloat3(&frustum_center);
                    const XMVECTOR shadow_eye = xcenter - light_direction * camera->far_plane;
                    const XMMATRIX shadow_view = XMMatrixLookToLH(shadow_eye, light_direction, light_up);

                    // a sphere bound keeps its radius as the camera rotates, so the texel size below stays constant
                    std::array<float3, 8> corners_light_space = {};
                    float3 cascade_center_ls = {};
                    for (uint32 corner_index = 0; corner_index < 8; ++corner_index)
                    {
                        XMStoreFloat3(&corners_light_space[corner_index], XMVector3TransformCoord(XMLoadFloat3(&frustum_corners[corner_index]), shadow_view));

                        cascade_center_ls.x += corners_light_space[corner_index].x;
                        cascade_center_ls.y += corners_light_space[corner_index].y;
                        cascade_center_ls.z += corners_light_space[corner_index].z;
                    }
                    cascade_center_ls.x /= 8.0f;
                    cascade_center_ls.y /= 8.0f;
                    cascade_center_ls.z /= 8.0f;

                    float cascade_radius_squared = 0.0f;
                    for (const float3& corner_light_space : corners_light_space)
                    {
                        const float3 center_to_corner = {
                            corner_light_space.x - cascade_center_ls.x,
                            corner_light_space.y - cascade_center_ls.y,
                            corner_light_space.z - cascade_center_ls.z
                        };
                        cascade_radius_squared = (std::max)(cascade_radius_squared, math::LengthSquared(center_to_corner));
                    }
                    const float cascade_radius = (std::max)(std::sqrt(cascade_radius_squared), 0.001f);

                    math::AABB caster_light_bound = {};
                    caster_light_bound.Invalidate();
                    if (gpu_scene.shadow_caster_world_bound.IsValid())
                    {
                        caster_light_bound = gpu_scene.shadow_caster_world_bound.TransformAABB(shadow_view);
                    }

                    const uint32 shadow_resolution = (std::max)(1u, static_cast<uint32>(light.shadow_map_resolution * options.shadow_resolution_scale));
                    const float texel_size = (cascade_radius * 2.0f) / static_cast<float>(shadow_resolution);

					// snap the cascade center to the nearest texel to avoid shimmering
                    cascade_center_ls.x = std::floor(cascade_center_ls.x / texel_size) * texel_size;
                    cascade_center_ls.y = std::floor(cascade_center_ls.y / texel_size) * texel_size;

                    const float min_x = cascade_center_ls.x - cascade_radius;
                    const float max_x = cascade_center_ls.x + cascade_radius;
                    const float min_y = cascade_center_ls.y - cascade_radius;
                    const float max_y = cascade_center_ls.y + cascade_radius;

                    float near_z = cascade_center_ls.z + cascade_radius + 10.0f;
                    float far_z = cascade_center_ls.z - cascade_radius - 10.0f;
                    if (caster_light_bound.IsValid())
                    {
                        near_z = caster_light_bound.max.z + 10.0f;
                        far_z = caster_light_bound.min.z - 10.0f;
                    }
                    if (near_z <= far_z)
                    {
                        near_z = far_z + 1.0f;
                    }

                    const XMMATRIX shadow_projection = XMMatrixOrthographicOffCenterLH(
                        min_x,
                        max_x,
                        min_y,
                        max_y,
                        near_z,
                        far_z);

                    ShaderShadowCascade shader_shadow_cascade = {};
                    shader_shadow_cascade.Init();
                    XMStoreFloat4x4(&shader_shadow_cascade.shadow_view_projection, shadow_view * shadow_projection);
                    shader_shadow_cascade.split_far = split_far;
                    shader_shadow_cascade.blend_band = light.shadow_cascade_blend;
                    shader_shadow_cascade.texel_world_size = texel_size;
                    shadow_resources.shader_shadow_cascades.push_back(shader_shadow_cascade);

                    RenderShadowSlice render_shadow_slice = {};
                    render_shadow_slice.light_index = light_index;
                    render_shadow_slice.view_projection = shader_shadow_cascade.shadow_view_projection;
                    render_shadow_slice.casting_frustum.FromVPMatrix(render_shadow_slice.view_projection);
                    shadow_resources.render_shadow_slices.push_back(render_shadow_slice);

                    rectpacker::Rect rect = {};
                    rect.id = static_cast<int>(shadow_resources.shader_shadow_cascades.size() - 1);
                    rect.w = static_cast<stbrp_coord>(shadow_resolution);
                    rect.h = static_cast<stbrp_coord>(shadow_resolution);
                    atlas_packer.AddRect(rect);
                }
            }

            if (!atlas_packer.rects.empty())
            {
                if (!atlas_packer.Pack(16384))
                {
                    backlog::Post("failed to pack shadow map atlas", backlog::LogLevel::Error);
                    shadow_resources.shadow_map_atlas_size = { 0, 0 };
                    shadow_resources.shader_shadow_cascades.clear();
                    shadow_resources.render_shadow_slices.clear();
                    return;
                }

                shadow_resources.shadow_map_atlas_size = {
                    static_cast<uint32>(atlas_packer.width),
                    static_cast<uint32>(atlas_packer.height)
                };

                for (const rectpacker::Rect& rect : atlas_packer.rects)
                {
                    if (rect.was_packed == 0 || rect.id < 0)
                    {
                        continue;
                    }

                    ShaderShadowCascade& shader_shadow_cascade = shadow_resources.shader_shadow_cascades[rect.id];
                    shader_shadow_cascade.shadow_atlas_scale_bias = {
                        static_cast<float>(rect.w) / static_cast<float>(shadow_resources.shadow_map_atlas_size.x),
                        static_cast<float>(rect.h) / static_cast<float>(shadow_resources.shadow_map_atlas_size.y),
                        static_cast<float>(rect.x) / static_cast<float>(shadow_resources.shadow_map_atlas_size.x),
                        static_cast<float>(rect.y) / static_cast<float>(shadow_resources.shadow_map_atlas_size.y)
                    };

                    RenderShadowSlice& render_shadow_slice = shadow_resources.render_shadow_slices[rect.id];
                    render_shadow_slice.shadow_map_atlas_rect = { rect.x, rect.y, rect.w, rect.h };
                }
            }
        }
    }

    void View::BuildForwardLightList()
    {
        if (render_path_type == RenderPathType::Forward)
        {
            light_resources.visible_forward_lights.clear();

            const ecs::CameraComponent* camera = scene ? scene->GetComponent<ecs::CameraComponent>(camera_entity) : nullptr;
            if (!camera)
            {
                return;
            }

            rendering::GPUScene& gpu_scene = scene->GetGPUScene();
            const uint32 light_count = static_cast<uint32>(gpu_scene.shader_lights.size());
            for (uint32 i = gpu_scene.directional_count; i < light_count; ++i)
            {
                if (!gpu_scene.light_bounds[i].IntersectFrustum(camera->frustum))
                {
                    continue;
                }

                light_resources.visible_forward_lights.push_back(i);
                if (light_resources.visible_forward_lights.size() >= NUM_MAX_LIGHTS_FORWARD_RENDERING)
                {
                    backlog::Post("Per-view forward light count reached the limit; remaining lights culled", backlog::LogLevel::Warning);
                    break;
                }
            }
        }
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
            const auto& cull_data = gpu_scene.opaque_cull_data;
            sorted_opaque_indices.clear();
            for (uint32 i = 0; i < static_cast<uint32>(cull_data.size()); ++i)
            {
                const auto& c = cull_data[i];
                if ((culling_mask & c.layer_mask) == 0)
                    continue;
                if (frustum && c.aabb.IsValid() && !c.aabb.IntersectFrustum(*frustum))
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
            const auto& renderables = gpu_scene.opaque_renderables;
            const auto& cull_data = gpu_scene.opaque_cull_data;
            const Size slice_count = shadow_resources.render_shadow_slices.size();
            shadow_resources.caster_slice_ranges.assign(slice_count, uint2{ 0, 0 });
            shadow_resources.caster_slice_scratch.resize(slice_count);

            jobsystem::Context slice_ctx;
            for (Size slice_index = 0; slice_index < slice_count; ++slice_index)
            {
                Vector<uint32>& slice_casters = shadow_resources.caster_slice_scratch[slice_index];
                slice_casters.clear();

                const RenderShadowSlice& shadow_slice = shadow_resources.render_shadow_slices[slice_index];
                if (!shadow_slice.HasShadowMapAtlasRect())
                    continue;

                jobsystem::Execute(slice_ctx, [&, &slice_casters = slice_casters, &shadow_slice = shadow_slice](jobsystem::JobArgs)
                {
                    for (uint32 i = 0; i < static_cast<uint32>(cull_data.size()); ++i)
                    {
                        const auto& c = cull_data[i];
                        if ((culling_mask & c.layer_mask) == 0)
                            continue;
                        if ((c.flags & Renderable::CastShadow) == 0)
                            continue;
                        if (options.enable_frustum_culling && c.aabb.IsValid() && !c.aabb.IntersectFrustum(shadow_slice.casting_frustum))
                            continue;
                        slice_casters.push_back(i);
                    }

                    std::sort(slice_casters.begin(), slice_casters.end(),
                        [&](uint32 a, uint32 b)
                        {
                            const auto& ra = renderables[a];
                            const auto& rb = renderables[b];
                            if (ra.push_constants.geometry_index != rb.push_constants.geometry_index)
                                return ra.push_constants.geometry_index < rb.push_constants.geometry_index;
                            if (ra.push_constants.material_index != rb.push_constants.material_index)
                                return ra.push_constants.material_index < rb.push_constants.material_index;
                            if (ra.IsDoubleSided() != rb.IsDoubleSided())
                                return ra.IsDoubleSided() < rb.IsDoubleSided();
                            return ra.primitive_topology < rb.primitive_topology;
                        });
                });
            }
            jobsystem::Wait(slice_ctx);

            sorted_shadow_caster_indices.clear();
            for (Size slice_index = 0; slice_index < slice_count; ++slice_index)
            {
                const Vector<uint32>& slice_casters = shadow_resources.caster_slice_scratch[slice_index];
                const uint32 range_begin = static_cast<uint32>(sorted_shadow_caster_indices.size());
                sorted_shadow_caster_indices.insert(sorted_shadow_caster_indices.end(), slice_casters.begin(), slice_casters.end()); // generally bigger than count of renderables
                shadow_resources.caster_slice_ranges[slice_index] = { range_begin, static_cast<uint32>(slice_casters.size()) };
            }
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
