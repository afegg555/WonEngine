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
#include "Console.h"
#include "ShaderInterop_Water.h"

#include <cmath>
#include <numeric>

namespace won::rendering
{
    static won::console::ConsoleVariable r_occlusion_bounds_expand("r.occlusion.bounds_expand", 0.005f, "occlusion query bounds expansion as a fraction of the distance to the bounds", won::console::ConsoleVariableFlagNone);

    static console::ConsoleVariable r_culling_log("r.culling.log", false, "log per-frame opaque culling counts for each stage", console::ConsoleVariableFlagNone);

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

        if (camera_entity == ecs::INVALID_ENTITY)
        {
            return false;
        }
        const ecs::CameraComponent* camera = &cached_camera;

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
            scene->GetSimulation().elapsed_seconds += static_cast<double>(delta_time);
            //scene->GetWaterSimulation().pending_steps = 0;
            if (!simulation_paused)
            {
                scene->Update(delta_time);
            }
        }

        camera_entity = ResolveCamera();
        if (ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(camera_entity))
        {
            if (camera->IsAutoExposure() && exposure_resources.measured_luminance >= 0.0f)
            {
                const float measured = (std::max)(1e-4f, exposure_resources.measured_luminance);

                // measured(y) = scene luminance(k) * current_exposure(x)
                // if we want to make measured as 0.18(middle gray)
                // (left) y * (0.18 / y) = 0.18
                // (right) k * x * (0.18 / y)

                // target_exposure(x') = x * (0.18 / y)
                float target = camera->exposure_multiplier * (ecs::CameraComponent::auto_exposure_target / measured);
                const float exposure_low = ecs::CameraComponent::ExposureFromEV100(camera->auto_exposure_max_ev);
                const float exposure_high = ecs::CameraComponent::ExposureFromEV100(camera->auto_exposure_min_ev);
                target = (std::min)((std::max)(target, exposure_low), exposure_high);
                const float lerp_factor = (std::min)(1.0f, (std::max)(0.0f, camera->auto_exposure_speed * 0.02f));
                camera->exposure_multiplier += (target - camera->exposure_multiplier) * lerp_factor;
            }
            cached_camera = *camera;
        }
        else
        {
            camera_entity = ecs::INVALID_ENTITY;
        }

        BuildShadowSlices();
        {
            auto sorted_range = profiler::ScopedRangeCPU("Build Sorted Indices");
            BuildSortedIndices();
            BuildForwardLightList();
        }

        BuildWaterTiles();
    }

    namespace
    {
        struct WaterQuadtree
        {
            float2 root_center = { 0.0f, 0.0f };
            float root_half_size = 0.0f;
            float3 camera = { 0.0f, 0.0f, 0.0f };
            float water_height = 0.0f;
            uint32 max_depth = 0;
            float distance_scale = 2.0f;

            bool ShouldSubdivide(float2 center, float half_size, uint32 depth) const
            {
                if (depth >= max_depth)
                {
                    return false;
                }
                const float nearest_x = (std::min)((std::max)(camera.x, center.x - half_size), center.x + half_size);
                const float nearest_z = (std::min)((std::max)(camera.z, center.y - half_size), center.y + half_size);
                const float dx = camera.x - nearest_x;
                const float dy = camera.y - water_height;
                const float dz = camera.z - nearest_z;
                const float threshold = half_size * 2.0f * distance_scale;
                return (dx * dx + dy * dy + dz * dz) < threshold * threshold;
            }

            uint32 DepthAt(float2 position) const
            {
                float2 center = root_center;
                float half_size = root_half_size;
                uint32 depth = 0;
                while (ShouldSubdivide(center, half_size, depth))
                {
                    half_size *= 0.5f;
                    center.x += position.x < center.x ? -half_size : half_size;
                    center.y += position.y < center.y ? -half_size : half_size;
                    ++depth;
                }
                return depth;
            }
        };
    }

    void View::BuildWaterTiles()
    {
        water_resources.tiles.clear();
        water_resources.zone_tile_ranges.clear();

        const rendering::GPUScene& gpu_scene = scene->GetGPUScene();
        if (gpu_scene.water.shader_zones.empty())
        {
            return;
        }

        const ecs::CameraComponent* camera = camera_entity != ecs::INVALID_ENTITY ? &cached_camera : nullptr;
        if (!camera)
        {
            return;
        }

        water_resources.zone_tile_ranges.resize(gpu_scene.water.shader_zones.size());
        for (Size zone_index = 0; zone_index < gpu_scene.water.shader_zones.size(); ++zone_index)
        {
            const ShaderWaterZone& zone = gpu_scene.water.shader_zones[zone_index];
            WaterResources::TileRange& tile_range = water_resources.zone_tile_ranges[zone_index];
            tile_range.first_tile = static_cast<uint32>(water_resources.tiles.size());
            tile_range.tile_count = 0;

            const float2 zone_min = zone.origin;
            const float2 zone_max = { zone.origin.x + zone.extent.x, zone.origin.y + zone.extent.y };

            WaterQuadtree quadtree = {};
            quadtree.root_center = { (zone_min.x + zone_max.x) * 0.5f, (zone_min.y + zone_max.y) * 0.5f };
            quadtree.root_half_size = (std::max)(zone.extent.x, zone.extent.y) * 0.5f;
            float height_min = 0.0f;
            float height_max = 0.0f;
            if (zone.body_count > 0)
            {
                height_min = gpu_scene.water.shader_bodies[zone.first_body].plane_origin.y;
                height_max = height_min;
                for (uint32 body = 1; body < zone.body_count; ++body)
                {
                    const float body_height = gpu_scene.water.shader_bodies[zone.first_body + body].plane_origin.y;
                    height_min = (std::min)(height_min, body_height);
                    height_max = (std::max)(height_max, body_height);
                }
            }

            quadtree.camera = camera->eye;
            quadtree.water_height = (height_min + height_max) * 0.5f;
            quadtree.max_depth = zone.lod_levels;
            quadtree.distance_scale = zone.lod_distance_scale;

            struct PendingTile
            {
                float2 center;
                float half_size;
                uint32 depth;
				uint32 parent_corner; // 0=negXnegZ, 1=posXnegZ, 2=negXposZ, 3=posXposZ, 0xFFFFFFFF=no parent
            };

            Vector<PendingTile> pending;
            pending.push_back({ quadtree.root_center, quadtree.root_half_size, 0u, 0u });

            while (!pending.empty())
            {
                const PendingTile tile = pending.back();
                pending.pop_back();

                if (tile.center.x + tile.half_size < zone_min.x || tile.center.x - tile.half_size > zone_max.x
                    || tile.center.y + tile.half_size < zone_min.y || tile.center.y - tile.half_size > zone_max.y)
                {
                    continue;
                }

                if (quadtree.ShouldSubdivide(tile.center, tile.half_size, tile.depth))
                {
                    const float child_half = tile.half_size * 0.5f;
                    for (uint32 corner = 0; corner < 4; ++corner)
                    {
                        const float2 child_center = {
                            tile.center.x + ((corner & 1u) ? child_half : -child_half),
                            tile.center.y + ((corner & 2u) ? child_half : -child_half)
                        };
                        pending.push_back({ child_center, child_half, tile.depth + 1u, corner });
                    }
                    continue;
                }

                bool covers_water = false;
                for (uint32 body = 0; body < zone.body_count; ++body)
                {
                    const ShaderWaterBody& water = gpu_scene.water.shader_bodies[zone.first_body + body];
                    const float reach_x = std::abs(water.axis_x.x) * water.half_extent_x + std::abs(water.axis_z.x) * water.half_extent_z;
                    const float reach_z = std::abs(water.axis_x.z) * water.half_extent_x + std::abs(water.axis_z.z) * water.half_extent_z;
                    covers_water = std::abs(water.plane_origin.x - tile.center.x) <= reach_x + tile.half_size
                        && std::abs(water.plane_origin.z - tile.center.y) <= reach_z + tile.half_size;

                    if(covers_water)
                    {
                        break;
					}
                }
                if (!covers_water)
                {
					// no water bodies intersect this tile
                    continue;
                }

				// see this T-junction problem discussion for why we need to check for coarser neighbors and mark them in a bitmask:
                // https://computergraphics.stackexchange.com/questions/1461/why-do-t-junctions-in-meshes-result-in-cracks
                
                uint32 mask = 0;
                if (tile.depth > 0)
                {
                    const bool probe_neg_x = (tile.parent_corner & 1u) == 0u;
                    const bool probe_pos_x = (tile.parent_corner & 1u) != 0u;
                    const bool probe_neg_z = (tile.parent_corner & 2u) == 0u;
                    const bool probe_pos_z = (tile.parent_corner & 2u) != 0u;

                    const float probe = tile.half_size * 1.5f;
                    if (probe_neg_x && quadtree.DepthAt({ tile.center.x - probe, tile.center.y }) < tile.depth) { mask |= WATER_TILE_NEIGHBOR_NEG_X; }
                    if (probe_pos_x && quadtree.DepthAt({ tile.center.x + probe, tile.center.y }) < tile.depth) { mask |= WATER_TILE_NEIGHBOR_POS_X; }
                    if (probe_neg_z && quadtree.DepthAt({ tile.center.x, tile.center.y - probe }) < tile.depth) { mask |= WATER_TILE_NEIGHBOR_NEG_Z; }
                    if (probe_pos_z && quadtree.DepthAt({ tile.center.x, tile.center.y + probe }) < tile.depth) { mask |= WATER_TILE_NEIGHBOR_POS_Z; }
                }

                ShaderWaterTile shader_tile = {};
                shader_tile.Init();
                shader_tile.center = tile.center;
                shader_tile.half_size = tile.half_size;
                shader_tile.coarser_neighbor_mask = mask;
                water_resources.tiles.push_back(shader_tile);
            }

            tile_range.tile_count = static_cast<uint32>(water_resources.tiles.size()) - tile_range.first_tile;
        }
    }

    void View::BuildShadowSlices()
    {
        shadow_resources.shader_shadow_cascades.clear();
        shadow_resources.render_shadow_slices.clear();
        shadow_resources.shadow_map_atlas_size = { 0, 0 };

        const ecs::CameraComponent* camera = camera_entity != ecs::INVALID_ENTITY ? &cached_camera : nullptr;
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
                    shader_shadow_cascade.depth_range = (std::max)(std::abs(near_z - far_z), 1.0f);
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

            const ecs::CameraComponent* camera = camera_entity != ecs::INVALID_ENTITY ? &cached_camera : nullptr;
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
        const ecs::CameraComponent* camera = camera_entity != ecs::INVALID_ENTITY ? &cached_camera : nullptr;
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
            const bool apply_occlusion = occlusion_resources.active;
            const float near_plane = camera ? camera->near_plane : 0.0f;
            const float bounds_expand_scale = r_occlusion_bounds_expand.GetFloat();
            sorted_opaque_indices.clear();
            occlusion_query_indices.clear();
            occlusion_resources.query_boxes.clear();
            uint32 layer_culled = 0;
            uint32 frustum_culled = 0;
            for (uint32 i = 0; i < static_cast<uint32>(cull_data.size()); ++i)
            {
                const auto& c = cull_data[i];
                if ((culling_mask & c.layer_mask) == 0)
                {
                    ++layer_culled;
                    continue;
                }
                if (frustum && c.aabb.IsValid() && !c.aabb.IntersectFrustum(*frustum))
                {
                    ++frustum_culled;
                    continue;
                }

                if (!apply_occlusion)
                {
                    sorted_opaque_indices.push_back(i);
                    continue;
                }

                const Renderable& renderable = renderables[i];
                bool queryable = false;
                if (renderable.aabb.IsValid())
                {
                    const float3 closest_point = {
                        (std::max)(renderable.aabb.min.x, (std::min)(eye.x, renderable.aabb.max.x)),
                        (std::max)(renderable.aabb.min.y, (std::min)(eye.y, renderable.aabb.max.y)),
                        (std::max)(renderable.aabb.min.z, (std::min)(eye.z, renderable.aabb.max.z))
                    };
                    const float distance_squared = math::DistanceSquared(closest_point, eye);
                    if (distance_squared > near_plane * near_plane)
                    {
                        const float bounds_expand = std::sqrt(distance_squared) * bounds_expand_scale;

                        ShaderOcclusionBox box = {};
                        box.Init();
                        box.aabb_min = {
                            renderable.aabb.min.x - bounds_expand,
                            renderable.aabb.min.y - bounds_expand,
                            renderable.aabb.min.z - bounds_expand
                        };
                        box.aabb_max = {
                            renderable.aabb.max.x + bounds_expand,
                            renderable.aabb.max.y + bounds_expand,
                            renderable.aabb.max.z + bounds_expand
                        };
                        occlusion_query_indices.push_back(i);
                        occlusion_resources.query_boxes.push_back(box);
                        queryable = true;
                    }
                }

                if (!queryable)
                {
                    sorted_opaque_indices.push_back(i);
                    continue;
                }

                const OcclusionResources::RenderableKey key = { renderable.entity, renderable.push_constants.geometry_index };
                const auto entry = occlusion_resources.visibility.find(key);
                if (entry != occlusion_resources.visibility.end() && entry->second.IsOccluded())
                {
                    continue;
                }

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

            if (r_culling_log.GetBool())
            {
                const uint32 total = static_cast<uint32>(cull_data.size());
                const uint32 drawn = static_cast<uint32>(sorted_opaque_indices.size());
                const uint32 occlusion_culled = total - layer_culled - frustum_culled - drawn;
                wonlog("culling: opaque %u, layer %u, frustum %u, occlusion %u, drawn %u",
                    total, layer_culled, frustum_culled, occlusion_culled, drawn);
            }
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

            sprite_resources.sprites_3d.clear();
            sprite_resources.sprites_3d.reserve(sorted_sprite_3d_indices.size());
            for (uint32 idx : sorted_sprite_3d_indices)
            {
                const auto& r = renderables[idx];

                ShaderSprite sprite = {};
                sprite.Init();
                sprite.size_pivot = { r.size.x, r.size.y, r.pivot.x, r.pivot.y };
                sprite.uv_rect = r.uv_rect;
                sprite.instance_index = r.instance_index;
                sprite.material_index = r.material_index;
                if (r.IsBillboard())
                {
                    sprite.flags |= SHADER_SPRITE_FLAG_BILLBOARD;
                }
                if (r.IsText())
                {
                    if (r.font && r.font->render_data.IsValid())
                    {
                        sprite.SetResourceIndex(static_cast<uint32>(r.font->render_data.atlas_srv.descriptor_index));
                    }
                }
                else if (r.IsParticle())
                {
                    sprite.flags |= SHADER_SPRITE_FLAG_PARTICLE;
                }
                sprite_resources.sprites_3d.push_back(sprite);
            }
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

            sprite_resources.sprites_2d.clear();
            sprite_resources.sprites_2d.reserve(sorted_sprite_2d_indices.size());
            for (uint32 idx : sorted_sprite_2d_indices)
            {
                const auto& r = renderables[idx];
                float s = 1.0f;
                if (r.reference_resolution.x > 0.0f && r.reference_resolution.y > 0.0f)
                {
                    s = std::pow(vp_w / r.reference_resolution.x, 1.0f - r.match) * std::pow(vp_h / r.reference_resolution.y, r.match);
                }
                const float px = r.anchor.x * vp_w + r.position.x * s;
                const float py = r.anchor.y * vp_h + r.position.y * s;

                ShaderSprite sprite = {};
                sprite.Init();
                sprite.size_pivot = { r.size.x * s, r.size.y * s, r.pivot.x, r.pivot.y };
                sprite.uv_rect = r.uv_rect;
                sprite.instance_index = math::PackHalf2(vp_w > 0.0f ? px / vp_w : 0.0f,
                                                        vp_h > 0.0f ? py / vp_h : 0.0f);
                sprite.material_index = r.material_index;
                if (r.IsText() && r.font && r.font->render_data.IsValid())
                {
                    sprite.SetResourceIndex(static_cast<uint32>(r.font->render_data.atlas_srv.descriptor_index));
                }
                sprite_resources.sprites_2d.push_back(sprite);
            }
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
