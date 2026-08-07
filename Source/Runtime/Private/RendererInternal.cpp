#include "RendererInternal.h"
#include "ShaderInterop_Sprite.h"
#include "ShaderInterop_Decal.h"
#ifndef WON_SHIPPING
#include "ShaderInterop_DebugDraw.h"
#endif

#include "BuiltinFont.h"
#include "Console.h"
#include "DebugDraw.h"
#include "LTCLUTData.h"

#include "Backlog.h"
#include "Timer.h"
#include "Profiler.h"
#include "RenderingUtils.h"
#include "Scene.h"
#include "GPUScene.h"
#include "ShaderLibrary.h"
#include "MathUtils.h"

#include "Window.h"
#include "Entity.h"
#include "CameraComponent.h"
#include "RectPacker.h"

#include "ShaderInterop.h"
#include "ShaderInterop_PostProcess.h"
#include "ShaderInterop_LightCull.h"

#include <algorithm>
#include <cstring>
#include <cmath>

using namespace won::resource;
using namespace won::ecs;
using namespace won::math;

namespace won::rendering
{
    namespace
    {
        RHIPrimitiveTopology ToRHIPrimitiveTopology(resource::PrimitiveTopology topology)
        {
            switch (topology)
            {
            case resource::PrimitiveTopology::LineList:
                return RHIPrimitiveTopology::LineList;
            case resource::PrimitiveTopology::PointList:
                return RHIPrimitiveTopology::PointList;
            case resource::PrimitiveTopology::TriangleList:
            default:
                return RHIPrimitiveTopology::TriangleList;
            }
        }
    }

    void RendererInternal::SetClearColor(const RHIClearColor& color)
    {
        clear_color = color;
    }

    RHIClearColor RendererInternal::GetClearColor() const
    {
        return clear_color;
    }

    void RendererInternal::SetVSync(bool enabled)
    {
        vsync_requested = enabled;
    }

    void RendererInternal::SetShadowResolutionScale(float scale)
    {
        shadow_resolution_scale = (std::max)(0.1f, scale);
    }

    bool RendererInternal::GetCurrentBackBufferBinding(RHISubresourceBinding& out_binding) const
    {
        if (!current_window)
        {
            return false;
        }

        RHISwapchain* swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            return false;
        }

        const uint32 back_buffer_index = swapchain->GetCurrentBackBufferIndex();
        RHIResource* back_buffer = swapchain->GetCurrentBackBuffer();
        if (!back_buffer || back_buffer_index >= back_buffers_rtv.size() || !back_buffers_rtv[back_buffer_index].IsValid())
        {
            return false;
        }

        out_binding.resource = back_buffer;
        out_binding.subresource = back_buffers_rtv[back_buffer_index];
        return true;
    }

    bool RendererInternal::CreateBackBufferSubresources()
    {
        RHISwapchain* swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            std::unique_ptr<RHISwapchain> created_swapchain = device->CreateSwapchain(*current_window);
            if (!created_swapchain)
            {
                backlog::Post("failed to create swapchain", backlog::LogLevel::Error);
                return false;
            }
            created_swapchain->SetVSync(vsync_enabled);
            swapchain = created_swapchain.get();
            current_window->SetRHISwapchain(std::move(created_swapchain));
        }

        for (uint32 i = 0; i < swapchain->GetBackBufferCount() && i < static_cast<uint32>(back_buffers_rtv.size()); ++i)
        {
            if (back_buffers_rtv[i].IsValid())
            {
                continue;
            }

            RHIResource* swapchain_back_buffer = swapchain->GetBackBuffer(i);
            if (!swapchain_back_buffer)
            {
                backlog::Post("failed to get swapchain back buffer for RTV creation", backlog::LogLevel::Error);
                return false;
            }

            RHISubresourceDesc back_buffer_subresource_desc = {};
            back_buffer_subresource_desc.type = RHISubresourceType::RenderTarget;
            if (!device->CreateSubresource(*swapchain_back_buffer, back_buffer_subresource_desc, &back_buffers_rtv[i]))
            {
                backlog::Post("failed to create back buffer RTV", backlog::LogLevel::Error);
                return false;
            }
        }

        return true;
    }

    bool RendererInternal::UpdateFrameConstants(FrameContext& frame_context, const View& view, RHICommandList& command_list)
    {
        ShaderFrame shader_frame{};
        shader_frame.Init();
        ShaderView shader_view{};
        shader_view.Init();
        rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();
        shader_frame.scene.instancebuffer = gpu_scene.instance_buffer.srv.descriptor_index;
        shader_frame.scene.geometrybuffer = gpu_scene.geometry_buffer.srv.descriptor_index;
        shader_frame.scene.materialbuffer = gpu_scene.material_buffer.srv.descriptor_index;
        shader_frame.scene.lightbuffer = gpu_scene.light_buffer.srv.descriptor_index;
        shader_frame.scene.directional_count = gpu_scene.directional_count;
        shader_frame.scene.light_count = static_cast<uint32>(gpu_scene.shader_lights.size()) - (gpu_scene.has_derived_sun ? 1u : 0u);
        if (view.render_path_type == RenderPathType::Forward && view.light_resources.forward_index_buffer && view.light_resources.forward_light_count > 0)
        {
            shader_view.forward_light_index_buffer = static_cast<int>(view.light_resources.forward_index_srv.descriptor_index);
            shader_view.forward_light_count = view.light_resources.forward_light_count;
        }
        if (view.render_path_type == RenderPathType::ForwardPlus && view.light_resources.cluster_light_count_buffer && view.light_resources.cluster_light_offset_buffer && view.light_resources.cluster_light_index_buffer)
        {
            shader_view.cluster_light_count_buffer = static_cast<int>(view.light_resources.cluster_light_count_srv.descriptor_index);
            shader_view.cluster_light_offset_buffer = static_cast<int>(view.light_resources.cluster_light_offset_srv.descriptor_index);
            shader_view.cluster_light_index_buffer = static_cast<int>(view.light_resources.cluster_light_index_srv.descriptor_index);
            shader_view.cluster_count = view.light_resources.cluster_dims;
            shader_view.cluster_depth_slices = view.light_resources.depth_slice_count;
        }
        shader_view.shadow_atlas = view.shadow_resources.atlas_srv.descriptor_index;
        shader_view.shadow_cascade_buffer = view.shadow_resources.cascade_srv.descriptor_index;
        shader_view.light_shadow_slice_buffer = view.shadow_resources.light_slice_srv.descriptor_index;
        shader_frame.scene.bvh_node_buffer = gpu_scene.bvh_node_buffer.srv.descriptor_index;
        shader_frame.scene.bvh_instance_buffer = gpu_scene.bvh_instance_buffer.srv.descriptor_index;
        shader_frame.scene.bvh_node_count = static_cast<uint32>(gpu_scene.shader_bvh_nodes.size());
        shader_frame.scene.bvh_instance_count = static_cast<uint32>(gpu_scene.shader_bvh_instances.size());
        shader_view.instance_sort_buffer = view.instance_resources.sort_srv.descriptor_index;
        shader_frame.scene.bone_matrix_buffer = gpu_scene.bone_buffer.srv.descriptor_index;
        shader_view.debug_view_mode = static_cast<uint32>(view.view_mode);
        shader_frame.scene.ltc_matrix_lut = ltc_matrix_lut ? static_cast<int>(ltc_matrix_lut_srv.descriptor_index) : -1;
        shader_frame.scene.ltc_fresnel_lut = ltc_fresnel_lut ? static_cast<int>(ltc_fresnel_lut_srv.descriptor_index) : -1;
        shader_frame.time = static_cast<float>(frame_time_seconds);
        shader_frame.environment = gpu_scene.shader_environment;
        shader_frame.environment.brdf_lut = brdf_lut ? static_cast<int>(brdf_lut_srv.descriptor_index) : -1;
        if (shader_frame.environment.diffuse_gi_mode == SHADER_DIFFUSE_GI_MODE_SKY)
        {
            shader_frame.environment.irradiance_cubemap = gpu_scene.sky_lighting.valid
                ? static_cast<int>(gpu_scene.sky_lighting.irradiance_srv.descriptor_index)
                : -1;
        }
        if (shader_frame.environment.reflection_mode == SHADER_REFLECTION_MODE_SKY)
        {
            const bool has_specular = gpu_scene.sky_lighting.valid && gpu_scene.sky_lighting.specular_texture;
            shader_frame.environment.specular_cubemap = has_specular
                ? static_cast<int>(gpu_scene.sky_lighting.specular_srv.descriptor_index)
                : -1;
            shader_frame.environment.specular_mip_count = has_specular ? static_cast<float>(sky_specular_mip_count) : 0.0f;
        }
        shader_frame.reflection_probe = gpu_scene.shader_reflection_probe;
        shader_frame.ddgi_volume = gpu_scene.shader_ddgi_volume;
        shader_frame.ddgi_volume.irradiance_texture = gpu_scene.ddgi.irradiance_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.irradiance_texture_uav = gpu_scene.ddgi.irradiance_texture_uav.descriptor_index;
        shader_frame.ddgi_volume.visibility_texture = gpu_scene.ddgi.visibility_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.visibility_texture_uav = gpu_scene.ddgi.visibility_texture_uav.descriptor_index;
        shader_frame.ddgi_volume.probe_data_buffer = gpu_scene.ddgi.probe_data_buffer_srv.descriptor_index;
        shader_frame.ddgi_volume.probe_data_buffer_uav = gpu_scene.ddgi.probe_data_buffer_uav.descriptor_index;
        shader_frame.ddgi_volume.previous_irradiance_texture = gpu_scene.ddgi.irradiance_history_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.previous_visibility_texture = gpu_scene.ddgi.visibility_history_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.previous_probe_data_buffer = gpu_scene.ddgi.probe_data_history_buffer_srv.descriptor_index;
        shader_frame.ddgi_volume.history_valid = gpu_scene.ddgi.history_valid ? 1u : 0u;
        shader_frame.ddgi_volume.probe_update_start = shader_frame.ddgi_volume.total_probe_count > 0 ? gpu_scene.ddgi.probe_update_offset % shader_frame.ddgi_volume.total_probe_count : 0;
        shader_frame.ddgi_volume.probes_per_frame = gpu_scene.ddgi.history_valid ? (std::min)(shader_frame.ddgi_volume.probes_per_frame, shader_frame.ddgi_volume.total_probe_count) : shader_frame.ddgi_volume.total_probe_count;
        shader_frame.ddgi_volume.probe_update_dispatch_width = (std::min)(shader_frame.ddgi_volume.probes_per_frame, 65535u);

        ShaderCamera& shader_camera = shader_view.camera;
        if (view.camera_entity != ecs::INVALID_ENTITY)
        {
            const ecs::CameraComponent* camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
            if (camera_component)
            {
                shader_camera.position = camera_component->eye;
                shader_camera.forward = camera_component->forward;
                shader_camera.up = camera_component->up;
                shader_camera.z_near = camera_component->near_plane;
                shader_camera.z_far = camera_component->far_plane;
                shader_camera.internal_resolution = { static_cast<uint32>(view.viewport.width), static_cast<uint32>(view.viewport.height) };
                shader_camera.internal_resolution_rcp = {
                    view.viewport.width > 0 ? 1.0f / static_cast<float>(view.viewport.width) : 0.0f,
                    view.viewport.height > 0 ? 1.0f / static_cast<float>(view.viewport.height) : 0.0f
                };
                shader_camera.viewport_offset = { static_cast<uint32>(view.viewport.x), static_cast<uint32>(view.viewport.y) };
                shader_camera.view = camera_component->view;
                shader_camera.projection = camera_component->projection;
                shader_camera.view_projection = camera_component->view_projection;
                shader_camera.inv_view_projection = camera_component->inv_view_projection;
				shader_camera.exposure = camera_component->exposure_multiplier * std::exp2(camera_component->exposure_compensation);
            }
        }

        if (!UpdateDefaultBuffer(frame_context, *shader_frame_buffer, &shader_frame, sizeof(ShaderFrame), RHIResourceState::ConstantBuffer, 0, command_list))
        {
            return false;
        }
        if (!UpdateDefaultBuffer(frame_context, *view.view_constants.buffer, &shader_view, sizeof(ShaderView), RHIResourceState::ConstantBuffer, 0, command_list))
        {
            return false;
        }

        return true;
    }

    static won::console::ConsoleVariable r_upload_budget("r.upload_budget", 8, "max queued resource uploads per frame, 0 = unlimited", won::console::ConsoleVariableFlagNone);
    static won::console::ConsoleVariable r_cluster_depth_slices("r.cluster.depth_slices", 32, "Forward+ cluster depth slices (1 = 2D tiled)", won::console::ConsoleVariableFlagArchive);

    bool RendererInternal::BuildViewResources(FrameContext& frame_context, View& view, RHICommandList& command_list)
    {
        if (!view.scene || view.camera_entity == INVALID_ENTITY)
        {
            return false;
        }

        View::RenderTargets& targets = view.render_targets;
        const uint32 width = static_cast<uint32>((std::max)(1, view.viewport.width));
        const uint32 height = static_cast<uint32>((std::max)(1, view.viewport.height));

        const bool targets_valid = targets.depth && targets.depth_dsv.IsValid() && targets.depth_srv.IsValid() &&
            targets.color[0] && targets.color[1] && targets.color_rtv[0].IsValid() && targets.color_rtv[1].IsValid() &&
            targets.width == width && targets.height == height;
        if (!targets_valid)
        {
        targets.width = width;
        targets.height = height;

        if (!view.view_constants.buffer)
        {
            RHIBufferDesc view_constants_desc = {};
            view_constants_desc.size = sizeof(ShaderView);
            view_constants_desc.usage = RHIResourceUsage::Default;
            view_constants_desc.bind_flags = RHIBindFlags::ConstantBuffer;
            view.view_constants.buffer = device->CreateBuffer(view_constants_desc);
            if (!view.view_constants.buffer)
            {
                backlog::Post("failed to create view constant buffer", backlog::LogLevel::Error);
                return false;
            }
            view.view_constants.buffer->SetName("View Constants");
            RHISubresourceDesc view_constants_cbv_desc = {};
            view_constants_cbv_desc.type = RHISubresourceType::ConstantBuffer;
            view_constants_cbv_desc.buffer_offset = 0;
            view_constants_cbv_desc.buffer_size = sizeof(ShaderView);
            if (!device->CreateSubresource(*view.view_constants.buffer, view_constants_cbv_desc, &view.view_constants.cbv))
            {
                backlog::Post("failed to create view constant buffer CBV", backlog::LogLevel::Error);
                view.view_constants.buffer = nullptr;
                return false;
            }
        }

        frame_context.RemoveResourceDeferred(targets.depth);
        RHITextureDesc depth_desc = {};
        depth_desc.width = width;
        depth_desc.height = height;
        depth_desc.depth = 1;
        depth_desc.mip_levels = 1;
        depth_desc.array_layers = 1;
        depth_desc.sample_count = 1;
        depth_desc.format = DEPTH_BUFFER_FORMAT;
        depth_desc.usage = RHIResourceUsage::Default;
        depth_desc.bind_flags = RHIBindFlags::DepthStencil | RHIBindFlags::ShaderResource;
        targets.depth = device->CreateTexture(depth_desc);
        if (!targets.depth)
        {
            backlog::Post("failed to create view depth buffer", backlog::LogLevel::Error);
            return false;
        }
        targets.depth->SetName("View Depth Buffer");

        targets.depth_dsv = {};
        targets.depth_srv = {};
        RHISubresourceDesc depth_subresource_desc = {};
        depth_subresource_desc.type = RHISubresourceType::DepthStencil;
        depth_subresource_desc.format = depth_desc.format;
        if (!device->CreateSubresource(*targets.depth, depth_subresource_desc, &targets.depth_dsv))
        {
            backlog::Post("failed to create view depth DSV", backlog::LogLevel::Error);
            targets.depth = nullptr;
            return false;
        }
        depth_subresource_desc.type = RHISubresourceType::ShaderResource;
        if (!device->CreateSubresource(*targets.depth, depth_subresource_desc, &targets.depth_srv))
        {
            backlog::Post("failed to create view depth SRV", backlog::LogLevel::Error);
            targets.depth = nullptr;
            return false;
        }

        for (uint32 i = 0; i < 2; ++i)
        {
            frame_context.RemoveResourceDeferred(targets.color[i]);

            RHITextureDesc color_desc = {};
            color_desc.width = width;
            color_desc.height = height;
            color_desc.depth = 1;
            color_desc.mip_levels = 1;
            color_desc.array_layers = 1;
            color_desc.sample_count = 1;
            color_desc.format = HDR_COLOR_BUFFER_FORMAT;
            color_desc.usage = RHIResourceUsage::Default;
            color_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess | RHIBindFlags::RenderTarget;
            color_desc.clear_color[0] = clear_color.r;
            color_desc.clear_color[1] = clear_color.g;
            color_desc.clear_color[2] = clear_color.b;
            color_desc.clear_color[3] = clear_color.a;
            targets.color[i] = device->CreateTexture(color_desc);
            if (!targets.color[i])
            {
                backlog::Post("failed to create view color buffer", backlog::LogLevel::Error);
                return false;
            }
            targets.color[i]->SetName(i == 0 ? "View Color Buffer 0 (Scene Color)" : "View Color Buffer 1");

            targets.color_srv[i] = {};
            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.format = color_desc.format;
            srv_desc.first_slice = 0;
            srv_desc.slice_count = 1;
            srv_desc.first_mip = 0;
            srv_desc.mip_count = 1;
            if (!device->CreateSubresource(*targets.color[i], srv_desc, &targets.color_srv[i]))
            {
                backlog::Post("failed to create view color SRV", backlog::LogLevel::Error);
                return false;
            }

            targets.color_uav[i] = {};
            RHISubresourceDesc uav_desc = {};
            uav_desc.type = RHISubresourceType::UnorderedAccess;
            uav_desc.format = color_desc.format;
            uav_desc.first_slice = 0;
            uav_desc.slice_count = 1;
            uav_desc.first_mip = 0;
            uav_desc.mip_count = 1;
            if (!device->CreateSubresource(*targets.color[i], uav_desc, &targets.color_uav[i]))
            {
                backlog::Post("failed to create view color UAV", backlog::LogLevel::Error);
                return false;
            }

            targets.color_rtv[i] = {};
            RHISubresourceDesc rtv_desc = {};
            rtv_desc.type = RHISubresourceType::RenderTarget;
            rtv_desc.format = color_desc.format;
            if (!device->CreateSubresource(*targets.color[i], rtv_desc, &targets.color_rtv[i]))
            {
                backlog::Post("failed to create view color RTV", backlog::LogLevel::Error);
                return false;
            }

            RHISubresourceBinding color_binding = {};
            color_binding.resource = targets.color[i].get();
            color_binding.subresource = targets.color_rtv[i];
            command_list.TransitionResource(*targets.color[i], RHIResourceState::RenderTarget);
            command_list.ClearRenderTarget(color_binding, clear_color);
        }
        }

        view.shadow_resources.shader_shadow_cascades.clear();
        view.shadow_resources.render_shadow_slices.clear();
        view.shadow_resources.shadow_map_atlas_size = { 0, 0 };

        const ecs::CameraComponent* camera = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
        auto light_array = view.scene->GetComponentArray<ecs::LightComponent>().get();
        rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();
        if ((view.show_flags & Show_Shadows) != 0 && camera)
        {
            const uint32 total_light_count = light_array ? static_cast<uint32>(light_array->GetSize()) : 0u;
            view.shadow_resources.light_shadow_slices.assign(gpu_scene.shader_lights.size(), 0u);
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

                const uint32 cascade_offset = static_cast<uint32>(view.shadow_resources.shader_shadow_cascades.size());
                view.shadow_resources.light_shadow_slices[slice_index] = (cascade_offset & 0xFFFFu) | ((cascade_count & 0xFFFFu) << 16u);

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

                    const uint32 shadow_resolution = (std::max)(1u, static_cast<uint32>(light.shadow_map_resolution * shadow_resolution_scale));
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
                    view.shadow_resources.shader_shadow_cascades.push_back(shader_shadow_cascade);

                    View::RenderShadowSlice render_shadow_slice = {};
                    render_shadow_slice.light_index = light_index;
                    render_shadow_slice.view_projection = shader_shadow_cascade.shadow_view_projection;
                    render_shadow_slice.casting_frustum.FromVPMatrix(render_shadow_slice.view_projection);
                    view.shadow_resources.render_shadow_slices.push_back(render_shadow_slice);

                    rectpacker::Rect rect = {};
                    rect.id = static_cast<int>(view.shadow_resources.shader_shadow_cascades.size() - 1);
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
                    return false;
                }

                view.shadow_resources.shadow_map_atlas_size = {
                    static_cast<uint32>(atlas_packer.width),
                    static_cast<uint32>(atlas_packer.height)
                };

                for (const rectpacker::Rect& rect : atlas_packer.rects)
                {
                    if (rect.was_packed == 0 || rect.id < 0)
                    {
                        continue;
                    }

                    ShaderShadowCascade& shader_shadow_cascade = view.shadow_resources.shader_shadow_cascades[rect.id];
                    shader_shadow_cascade.shadow_atlas_scale_bias = {
                        static_cast<float>(rect.w) / static_cast<float>(view.shadow_resources.shadow_map_atlas_size.x),
                        static_cast<float>(rect.h) / static_cast<float>(view.shadow_resources.shadow_map_atlas_size.y),
                        static_cast<float>(rect.x) / static_cast<float>(view.shadow_resources.shadow_map_atlas_size.x),
                        static_cast<float>(rect.y) / static_cast<float>(view.shadow_resources.shadow_map_atlas_size.y)
                    };

                    View::RenderShadowSlice& render_shadow_slice = view.shadow_resources.render_shadow_slices[rect.id];
                    render_shadow_slice.shadow_map_atlas_rect = { rect.x, rect.y, rect.w, rect.h };
                }
            }
        }

        // after the shadow slices exist: the caster list is culled against them
        {
            auto cpu_range = profiler::ScopedRangeCPU("Build Sorted Indices");
            view.BuildSortedIndices();
        }



        // shadow map atlas
        if (view.shadow_resources.shadow_map_atlas_size.x == 0 || view.shadow_resources.shadow_map_atlas_size.y == 0)
        {
            frame_context.RemoveResourceDeferred(view.shadow_resources.atlas);
            view.shadow_resources.atlas_dsv = {};
            view.shadow_resources.atlas_srv = {};
        }
        else if (!view.shadow_resources.atlas ||
            !view.shadow_resources.atlas_dsv.IsValid() ||
            !view.shadow_resources.atlas_srv.IsValid() ||
            view.shadow_resources.atlas->GetDesc().texture_desc.width != view.shadow_resources.shadow_map_atlas_size.x ||
            view.shadow_resources.atlas->GetDesc().texture_desc.height != view.shadow_resources.shadow_map_atlas_size.y)
        {
            frame_context.RemoveResourceDeferred(view.shadow_resources.atlas);
        RHITextureDesc shadow_map_atlas_desc = {};
        shadow_map_atlas_desc.width = view.shadow_resources.shadow_map_atlas_size.x;
        shadow_map_atlas_desc.height = view.shadow_resources.shadow_map_atlas_size.y;
        shadow_map_atlas_desc.depth = 1;
        shadow_map_atlas_desc.mip_levels = 1;
        shadow_map_atlas_desc.array_layers = 1;
        shadow_map_atlas_desc.sample_count = 1;
        shadow_map_atlas_desc.format = RHIFormat::D32Float;
        shadow_map_atlas_desc.usage = RHIResourceUsage::Default;
        shadow_map_atlas_desc.bind_flags = RHIBindFlags::DepthStencil | RHIBindFlags::ShaderResource;
        view.shadow_resources.atlas = device->CreateTexture(shadow_map_atlas_desc);
        if (!view.shadow_resources.atlas)
        {
            backlog::Post("failed to create shadow map atlas", backlog::LogLevel::Error);
            return false;
        }
        view.shadow_resources.atlas->SetName("Shadow Map Atlas");

        view.shadow_resources.atlas_dsv = {};
        RHISubresourceDesc shadow_map_atlas_subresource_desc = {};
        shadow_map_atlas_subresource_desc.type = RHISubresourceType::DepthStencil;
        shadow_map_atlas_subresource_desc.format = shadow_map_atlas_desc.format;
        if (!device->CreateSubresource(*view.shadow_resources.atlas, shadow_map_atlas_subresource_desc, &view.shadow_resources.atlas_dsv))
        {
            backlog::Post("failed to create shadow map atlas subresource", backlog::LogLevel::Error);
            view.shadow_resources.atlas = nullptr;
            return false;
        }
        shadow_map_atlas_subresource_desc.type = RHISubresourceType::ShaderResource;
        if (!device->CreateSubresource(*view.shadow_resources.atlas, shadow_map_atlas_subresource_desc, &view.shadow_resources.atlas_srv))
        {
            backlog::Post("failed to create shadow map atlas subresource", backlog::LogLevel::Error);
            view.shadow_resources.atlas = nullptr;
            return false;
        }

        }

        // per-view shadow gpu buffers
        const Vector<ShaderShadowCascade>& shader_shadow_cascades = view.shadow_resources.shader_shadow_cascades;
        const Size required_shadow_cascade_buffer_size = shader_shadow_cascades.size() * sizeof(ShaderShadowCascade);
        const Size required_shadow_slice_buffer_size = view.shadow_resources.light_shadow_slices.size() * sizeof(uint32);

        if (required_shadow_cascade_buffer_size == 0)
        {
            frame_context.RemoveResourceDeferred(view.shadow_resources.cascade_buffer);
            view.shadow_resources.cascade_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (view.shadow_resources.cascade_buffer)
            {
                current_default_buffer_size = view.shadow_resources.cascade_buffer->GetDesc().buffer_desc.size;
            }

            if (!view.shadow_resources.cascade_buffer || current_default_buffer_size < required_shadow_cascade_buffer_size)
            {
                frame_context.RemoveResourceDeferred(view.shadow_resources.cascade_buffer);
                RHIBufferDesc cascade_buffer_desc = {};
                cascade_buffer_desc.size = required_shadow_cascade_buffer_size;
                cascade_buffer_desc.usage = RHIResourceUsage::Default;
                cascade_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                view.shadow_resources.cascade_buffer = device->CreateBuffer(cascade_buffer_desc);
                if (!view.shadow_resources.cascade_buffer)
                {
                    backlog::Post("failed to create view shadow cascade buffer", backlog::LogLevel::Error);
                    return false;
                }
                view.shadow_resources.cascade_buffer->SetName("View Shadow Cascade Buffer");

                view.shadow_resources.cascade_srv = {};
                RHISubresourceDesc cascade_srv_desc = {};
                cascade_srv_desc.type = RHISubresourceType::ShaderResource;
                cascade_srv_desc.buffer_offset = 0;
                cascade_srv_desc.buffer_size = view.shadow_resources.cascade_buffer->GetDesc().buffer_desc.size;
                cascade_srv_desc.buffer_stride = sizeof(ShaderShadowCascade);
                if (!device->CreateSubresource(*view.shadow_resources.cascade_buffer, cascade_srv_desc, &view.shadow_resources.cascade_srv))
                {
                    backlog::Post("failed to create view shadow cascade subresource", backlog::LogLevel::Error);
                    view.shadow_resources.cascade_buffer = nullptr;
                    return false;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *view.shadow_resources.cascade_buffer, shader_shadow_cascades.data(), required_shadow_cascade_buffer_size, RHIResourceState::ShaderRead, 0, command_list))
            {
                return false;
            }
        }
        if (required_shadow_slice_buffer_size == 0)
        {
            frame_context.RemoveResourceDeferred(view.shadow_resources.light_slice_buffer);
            view.shadow_resources.light_slice_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (view.shadow_resources.light_slice_buffer)
            {
                current_default_buffer_size = view.shadow_resources.light_slice_buffer->GetDesc().buffer_desc.size;
            }

            if (!view.shadow_resources.light_slice_buffer || current_default_buffer_size < required_shadow_slice_buffer_size)
            {
                frame_context.RemoveResourceDeferred(view.shadow_resources.light_slice_buffer);
                RHIBufferDesc shadow_slice_buffer_desc = {};
                shadow_slice_buffer_desc.size = required_shadow_slice_buffer_size;
                shadow_slice_buffer_desc.usage = RHIResourceUsage::Default;
                shadow_slice_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                view.shadow_resources.light_slice_buffer = device->CreateBuffer(shadow_slice_buffer_desc);
                if (!view.shadow_resources.light_slice_buffer)
                {
                    backlog::Post("failed to create light shadow slice buffer", backlog::LogLevel::Error);
                    return false;
                }
                view.shadow_resources.light_slice_buffer->SetName("View Light Shadow Slice Buffer");

                view.shadow_resources.light_slice_srv = {};
                RHISubresourceDesc shadow_slice_srv_desc = {};
                shadow_slice_srv_desc.type = RHISubresourceType::ShaderResource;
                shadow_slice_srv_desc.buffer_offset = 0;
                shadow_slice_srv_desc.buffer_size = view.shadow_resources.light_slice_buffer->GetDesc().buffer_desc.size;
                shadow_slice_srv_desc.buffer_stride = sizeof(uint32);
                if (!device->CreateSubresource(*view.shadow_resources.light_slice_buffer, shadow_slice_srv_desc, &view.shadow_resources.light_slice_srv))
                {
                    backlog::Post("failed to create light shadow slice subresource", backlog::LogLevel::Error);
                    view.shadow_resources.light_slice_buffer = nullptr;
                    return false;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *view.shadow_resources.light_slice_buffer, view.shadow_resources.light_shadow_slices.data(), required_shadow_slice_buffer_size, RHIResourceState::ShaderRead, 0, command_list))
            {
                return false;
            }
        }
        // per-view instance sort buffer
        {
            //rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();
            const auto& opaque = gpu_scene.opaque_renderables;
            const auto& transparent = gpu_scene.transparent_renderables;
            const uint32 opaque_count = static_cast<uint32>(view.sorted_opaque_indices.size());
            const uint32 transparent_count = static_cast<uint32>(view.sorted_transparent_indices.size());
            const uint32 shadow_caster_count = static_cast<uint32>(view.sorted_shadow_caster_indices.size());
            const Size required_sort_buffer_size = (opaque_count + transparent_count + shadow_caster_count) * sizeof(uint32);

            if (required_sort_buffer_size == 0)
            {
                frame_context.RemoveResourceDeferred(view.instance_resources.sort_buffer);
                view.instance_resources.sort_srv = {};
            }
            else
            {
                Size current_default_size = view.instance_resources.sort_buffer
                    ? view.instance_resources.sort_buffer->GetDesc().buffer_desc.size : 0;

                if (!view.instance_resources.sort_buffer || current_default_size < required_sort_buffer_size)
                {
                    frame_context.RemoveResourceDeferred(view.instance_resources.sort_buffer);
                    RHIBufferDesc desc = {};
                    desc.size = required_sort_buffer_size;
                    desc.usage = RHIResourceUsage::Default;
                    desc.bind_flags = RHIBindFlags::ShaderResource;
                    view.instance_resources.sort_buffer = device->CreateBuffer(desc);
                    if (!view.instance_resources.sort_buffer)
                    {
                        backlog::Post("failed to create shader instance sort default buffer", backlog::LogLevel::Error);
                        return false;
                    }
                    view.instance_resources.sort_buffer->SetName("Shader Instance Sort Default Buffer");

                    view.instance_resources.sort_srv = {};
                    RHISubresourceDesc srv_desc = {};
                    srv_desc.type = RHISubresourceType::ShaderResource;
                    srv_desc.buffer_offset = 0;
                    srv_desc.buffer_size = required_sort_buffer_size;
                    srv_desc.buffer_stride = sizeof(uint32);
                    if (!device->CreateSubresource(*view.instance_resources.sort_buffer, srv_desc, &view.instance_resources.sort_srv))
                    {
                        backlog::Post("failed to create shader instance sort subresource", backlog::LogLevel::Error);
                        view.instance_resources.sort_buffer = nullptr;
                        return false;
                    }
                }

                std::unique_ptr<RHIResource>& sort_upload_buffer = view.instance_resources.sort_upload_buffers[current_frame_slot];
                Size current_upload_size = sort_upload_buffer ? sort_upload_buffer->GetDesc().buffer_desc.size : 0;

                if (!sort_upload_buffer || current_upload_size < required_sort_buffer_size)
                {
                    frame_context.RemoveResourceDeferred(sort_upload_buffer);
                    RHIBufferDesc upload_desc = {};
                    upload_desc.size = required_sort_buffer_size;
                    upload_desc.usage = RHIResourceUsage::Upload;
                    upload_desc.bind_flags = RHIBindFlags::None;
                    sort_upload_buffer = device->CreateBuffer(upload_desc);
                    if (!sort_upload_buffer)
                    {
                        backlog::Post("failed to create shader instance sort upload buffer", backlog::LogLevel::Error);
                        return false;
                    }
                    sort_upload_buffer->SetName("Shader Instance Sort Upload Buffer");
                }

                uint32* mapped = static_cast<uint32*>(sort_upload_buffer->GetMappedData());
                if (!mapped)
                {
                    backlog::Post("failed to access mapped instance sort upload buffer", backlog::LogLevel::Error);
                    return false;
                }
                for (uint32 i = 0; i < opaque_count; ++i)
                    mapped[i] = opaque[view.sorted_opaque_indices[i]].push_constants.draw_offset;
                for (uint32 i = 0; i < transparent_count; ++i)
                    mapped[opaque_count + i] = transparent[view.sorted_transparent_indices[i]].push_constants.draw_offset;
                for (uint32 i = 0; i < shadow_caster_count; ++i)
                    mapped[opaque_count + transparent_count + i] = opaque[view.sorted_shadow_caster_indices[i]].push_constants.draw_offset;

                command_list.TransitionResource(*view.instance_resources.sort_buffer, RHIResourceState::CopyDest);
                command_list.CopyBuffer(*view.instance_resources.sort_buffer, 0, *sort_upload_buffer, 0, required_sort_buffer_size);
                command_list.TransitionResource(*view.instance_resources.sort_buffer, RHIResourceState::ShaderRead);
            }
        }

        // per-view light culling resources
        if (view.render_path_type == RenderPathType::ForwardPlus)
        {
            const uint32 tiles_x = (static_cast<uint32>(view.viewport.width) + LIGHTCULL_TILE_SIZE - 1) / LIGHTCULL_TILE_SIZE;
            const uint32 tiles_y = (static_cast<uint32>(view.viewport.height) + LIGHTCULL_TILE_SIZE - 1) / LIGHTCULL_TILE_SIZE;
            const int cluster_depth_slices_requested = r_cluster_depth_slices.GetInt();
            const uint32 depth_slices = cluster_depth_slices_requested < 1 ? 1u : (std::min)(static_cast<uint32>(cluster_depth_slices_requested), static_cast<uint32>(MAX_DEPTH_SLICES));
            if (tiles_x > 0 && tiles_y > 0 && (!view.light_resources.cluster_light_count_buffer || !view.light_resources.cluster_light_offset_buffer || !view.light_resources.cluster_light_index_buffer || view.light_resources.cluster_dims.x != tiles_x || view.light_resources.cluster_dims.y != tiles_y || view.light_resources.depth_slice_count != depth_slices))
            {
                const uint32 cluster_count = tiles_x * tiles_y * depth_slices;

                RHIBufferDesc grid_desc = {};
                grid_desc.size = static_cast<Size>(cluster_count) * sizeof(uint32);
                grid_desc.usage = RHIResourceUsage::Default;
                grid_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                frame_context.RemoveResourceDeferred(view.light_resources.cluster_light_count_buffer);
                frame_context.RemoveResourceDeferred(view.light_resources.cluster_light_offset_buffer);
                view.light_resources.cluster_light_count_buffer = device->CreateBuffer(grid_desc);
                view.light_resources.cluster_light_offset_buffer = device->CreateBuffer(grid_desc);

                RHIBufferDesc index_desc = {};
                index_desc.size = static_cast<Size>(cluster_count) * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32);
                index_desc.usage = RHIResourceUsage::Default;
                index_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                frame_context.RemoveResourceDeferred(view.light_resources.cluster_light_index_buffer);
                view.light_resources.cluster_light_index_buffer = device->CreateBuffer(index_desc);

                if (view.light_resources.cluster_light_count_buffer && view.light_resources.cluster_light_offset_buffer && view.light_resources.cluster_light_index_buffer)
                {
                    view.light_resources.cluster_light_count_buffer->SetName("Cluster Light Count");
                    view.light_resources.cluster_light_offset_buffer->SetName("Cluster Light Offset");
                    view.light_resources.cluster_light_index_buffer->SetName("Cluster Light Index List");

                    RHISubresourceDesc grid_uav_desc = {};
                    grid_uav_desc.type = RHISubresourceType::UnorderedAccess;
                    grid_uav_desc.buffer_offset = 0;
                    grid_uav_desc.buffer_size = grid_desc.size;
                    grid_uav_desc.buffer_stride = sizeof(uint32);
                    device->CreateSubresource(*view.light_resources.cluster_light_count_buffer, grid_uav_desc, &view.light_resources.cluster_light_count_uav);
                    RHISubresourceDesc grid_srv_desc = grid_uav_desc;
                    grid_srv_desc.type = RHISubresourceType::ShaderResource;
                    device->CreateSubresource(*view.light_resources.cluster_light_count_buffer, grid_srv_desc, &view.light_resources.cluster_light_count_srv);

                    device->CreateSubresource(*view.light_resources.cluster_light_offset_buffer, grid_uav_desc, &view.light_resources.cluster_light_offset_uav);
                    device->CreateSubresource(*view.light_resources.cluster_light_offset_buffer, grid_srv_desc, &view.light_resources.cluster_light_offset_srv);

                    RHISubresourceDesc index_uav_desc = {};
                    index_uav_desc.type = RHISubresourceType::UnorderedAccess;
                    index_uav_desc.buffer_offset = 0;
                    index_uav_desc.buffer_size = index_desc.size;
                    index_uav_desc.buffer_stride = sizeof(uint32);
                    device->CreateSubresource(*view.light_resources.cluster_light_index_buffer, index_uav_desc, &view.light_resources.cluster_light_index_uav);
                    RHISubresourceDesc index_srv_desc = index_uav_desc;
                    index_srv_desc.type = RHISubresourceType::ShaderResource;
                    device->CreateSubresource(*view.light_resources.cluster_light_index_buffer, index_srv_desc, &view.light_resources.cluster_light_index_srv);

                    view.light_resources.cluster_dims = { tiles_x, tiles_y };
                    view.light_resources.depth_slice_count = depth_slices;
                }
            }
        }
        else if (view.render_path_type == RenderPathType::Forward)
        {
            UpdateForwardLightList(view, command_list);
        }

        const RHIResource* ddgi_probe_data_buffer = view.scene->GetGPUScene().ddgi.probe_data_buffer.get();
        const bool ddgi_debug_wanted = (view.show_flags & Show_DDGI) != 0 && ddgi_probe_data_buffer;
        if (ddgi_debug_wanted && !view.ddgi_debug_resources.probe_data_readback_buffer)
        {
            RHIBufferDesc readback_desc = {};
            readback_desc.size = ddgi_probe_data_buffer->GetDesc().buffer_desc.size;
            readback_desc.usage = RHIResourceUsage::Readback;
            view.ddgi_debug_resources.probe_data_readback_buffer = device->CreateBuffer(readback_desc);
            if (view.ddgi_debug_resources.probe_data_readback_buffer)
            {
                view.ddgi_debug_resources.probe_data_readback_buffer->SetName("DDGI Debug Probe Data Readback Buffer");
            }
            view.ddgi_debug_resources.probe_data_readback_valid = false;
        }
        else if (!ddgi_debug_wanted && view.ddgi_debug_resources.probe_data_readback_buffer)
        {
            frame_context.RemoveResourceDeferred(view.ddgi_debug_resources.probe_data_readback_buffer);
            view.ddgi_debug_resources.probe_data_readback_valid = false;
        }

        return true;
    }


    bool RendererInternal::DrawScene(const FrameContext& frame_context, const View& view, RenderPassType pass, uint32 flags, RHICommandList& command_list, uint32 shadow_slice_index)
    {
        rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();

        RHICompareOp depth_compare = RHICompareOp::GreaterEqual;
        const bool draw_wireframe = pass == RenderPassType::MainPass && view.view_mode == ViewMode::Wireframe;
        const bool draw_overdraw = pass == RenderPassType::MainPass && view.view_mode == ViewMode::Overdraw;
        const bool draw_primitives = pass == RenderPassType::PrimitivePass && (flags & DrawScene_Primitive) != 0;
        if (pass == RenderPassType::MainPass)
        {
            if (draw_overdraw)
                depth_compare = RHICompareOp::Always;
            else if (!draw_wireframe)
                depth_compare = RHICompareOp::Equal;
        }

        GraphicsPipelineHash pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(pass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(draw_wireframe ? RHIFillMode::Wireframe : RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(depth_compare);

        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_cbv;

        RHISubresourceBinding shader_view_binding = {};
        shader_view_binding.resource = view.view_constants.buffer.get();
        shader_view_binding.subresource = view.view_constants.cbv;

        auto flush_batch = [&](const Vector<Renderable>& renderables, const Vector<uint32>& sort_indices, uint32 sort_buffer_base, uint32 start, uint32 size)
        {
            if (size == 0)
                return;
            const auto& first = renderables[sort_indices[start]];
            ObjectPushConstants push = first.push_constants;
            push.draw_offset = sort_buffer_base + start; // starting offset of sort_indices
            command_list.SetIndexBuffer(*first.index_buffer, sizeof(uint32), first.index_offset, first.index_count * sizeof(uint32));
            command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(first.primitive_topology));
            command_list.PushConstants(RHIShaderStage::Vertex, &push, sizeof(ObjectPushConstants), 0);
            command_list.DrawIndexed(first.index_count, size, 0, 0, 0);
        };

        if ((flags & DrawScene_Opaque) != 0 && !gpu_scene.opaque_renderables.empty())
        {
            // The shadow pass draws casters visible to the light, not to the camera, so it walks the range its cascade owns.
            const bool shadow_pass = pass == RenderPassType::ShadowPass;
            const Vector<uint32>& opaque_sort_indices = shadow_pass ? view.sorted_shadow_caster_indices : view.sorted_opaque_indices;
            uint32 opaque_sort_buffer_base = 0;
            uint32 sort_begin = 0;
            uint32 sort_end = static_cast<uint32>(opaque_sort_indices.size());
            if (shadow_pass)
            {
                if (shadow_slice_index >= view.shadow_resources.caster_slice_ranges.size())
                    return true;

                const uint2 slice_range = view.shadow_resources.caster_slice_ranges[shadow_slice_index];
                opaque_sort_buffer_base = static_cast<uint32>(view.sorted_opaque_indices.size() + view.sorted_transparent_indices.size());
                sort_begin = slice_range.x;
                sort_end = slice_range.x + slice_range.y;
            }

            uint32 batch_geometry_index = 0;
            uint32 batch_material_index = 0;
            uint32 batch_start = 0;
            uint32 batch_size = 0;
            GraphicsPipelineHash current_hash = {};
            bool has_pipeline = false;

            for (uint32 i = sort_begin; i < sort_end; ++i)
            {
                const Renderable& renderable = gpu_scene.opaque_renderables[opaque_sort_indices[i]];

                // The prepass has no alpha test, so masked materials write their own depth in the main pass.
                if (pass == RenderPassType::DepthPrepass && renderable.blend_mode == resource::MaterialBlendMode::Masked)
                {
                    flush_batch(gpu_scene.opaque_renderables, opaque_sort_indices, opaque_sort_buffer_base, batch_start, batch_size);
                    batch_size = 0;
                    continue;
                }

                const bool can_extend = batch_size > 0
                    && renderable.push_constants.geometry_index == batch_geometry_index
                    && renderable.push_constants.material_index == batch_material_index;

                if (!can_extend)
                {
                    flush_batch(gpu_scene.opaque_renderables, opaque_sort_indices, opaque_sort_buffer_base, batch_start, batch_size);
                    batch_size = 0;

                    GraphicsPipelineHash renderable_hash = pipeline_hash;
                    renderable_hash.storage.bits.cull_mode = static_cast<uint64>(
                        renderable.IsDoubleSided() ? RHICullMode::None : RHICullMode::Back);
                    if (pass == RenderPassType::MainPass)
                    {
                        renderable_hash.storage.bits.shader_type = draw_wireframe ? SHADER_MATERIAL_TYPE_UNLIT : renderable.shader_type;
                        renderable_hash.storage.bits.blend_mode = static_cast<uint64>(renderable.blend_mode);
                        if (renderable.blend_mode == resource::MaterialBlendMode::Masked)
                        {
                            renderable_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                        }
                    }
                    if (draw_overdraw)
                    {
                        renderable_hash.storage.bits.blend_mode = static_cast<uint64>(resource::MaterialBlendMode::Additive);
                    }
                    if (pass == RenderPassType::MainPass && view.render_path_type == RenderPathType::ForwardPlus && renderable_hash.storage.bits.shader_type == SHADER_MATERIAL_TYPE_PBR)
                    {
                        renderable_hash.storage.bits.clustered = 1;
                    }

                    if (!has_pipeline || !(current_hash == renderable_hash))
                    {
                        RHIPipeline* pipeline = shader_library.GetPipeline(renderable_hash);
                        if (!pipeline)
                            continue;
                        command_list.SetGraphicsPipeline(*pipeline);
                        command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                        command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                        command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                        command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                        current_hash = renderable_hash;
                        has_pipeline = true;
                    }

                    batch_geometry_index = renderable.push_constants.geometry_index;
                    batch_material_index = renderable.push_constants.material_index;
                    batch_start = i;
                    batch_size = 1;
                }
                else
                {
                    ++batch_size;
                }
            }
            flush_batch(gpu_scene.opaque_renderables, opaque_sort_indices, opaque_sort_buffer_base, batch_start, batch_size);
        }

        if ((flags & DrawScene_Transparent) != 0 && !gpu_scene.transparent_renderables.empty())
        {
            const uint32 sort_buffer_base = static_cast<uint32>(view.sorted_opaque_indices.size());

            GraphicsPipelineHash current_hash = {};
            bool has_pipeline = false;

            for (uint32 i = 0; i < static_cast<uint32>(view.sorted_transparent_indices.size()); ++i)
            {
                const Renderable& renderable = gpu_scene.transparent_renderables[view.sorted_transparent_indices[i]];

                if (pass == RenderPassType::ShadowPass && !renderable.IsCastShadow())
                    continue;

                GraphicsPipelineHash renderable_hash = pipeline_hash;
                renderable_hash.storage.bits.cull_mode = static_cast<uint64>(
                    renderable.IsDoubleSided() ? RHICullMode::None : RHICullMode::Back);
                if (pass == RenderPassType::MainPass)
                {
                    renderable_hash.storage.bits.shader_type = draw_wireframe ? SHADER_MATERIAL_TYPE_UNLIT : renderable.shader_type;
                    renderable_hash.storage.bits.blend_mode = static_cast<uint64>(draw_overdraw ? resource::MaterialBlendMode::Additive : renderable.blend_mode);
                    renderable_hash.storage.bits.depth_compare = static_cast<uint64>(draw_overdraw ? RHICompareOp::Always : RHICompareOp::GreaterEqual);
                    if (view.render_path_type == RenderPathType::ForwardPlus && renderable_hash.storage.bits.shader_type == SHADER_MATERIAL_TYPE_PBR)
                    {
                        renderable_hash.storage.bits.clustered = 1;
                    }
                }

                if (!has_pipeline || !(current_hash == renderable_hash))
                {
                    RHIPipeline* pipeline = shader_library.GetPipeline(renderable_hash);
                    if (!pipeline)
                        continue;
                    command_list.SetGraphicsPipeline(*pipeline);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                    current_hash = renderable_hash;
                    has_pipeline = true;
                }

                ObjectPushConstants push = renderable.push_constants;
                push.draw_offset = sort_buffer_base + i;
                command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_offset, renderable.index_count * sizeof(uint32));
                command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                command_list.PushConstants(RHIShaderStage::Vertex, &push, sizeof(ObjectPushConstants), 0);
                command_list.DrawIndexed(renderable.index_count, 1, 0, 0, 0);
            }
        }

		if (draw_primitives) // line, point
        {
            GraphicsPipelineHash line_pipeline_hash = pipeline_hash;
            line_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::LineList);
            line_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            line_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            line_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

            RHIPipeline* line_pipeline = shader_library.GetPipeline(line_pipeline_hash);
            if (line_pipeline)
            {
                command_list.SetGraphicsPipeline(*line_pipeline);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);

                for (const auto& renderable : gpu_scene.line_renderables)
                {
                    command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_offset, renderable.index_count * sizeof(uint32));
                    command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                    command_list.PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                    command_list.DrawIndexed(renderable.index_count, 1, 0, 0, 0);
                }
            }

            GraphicsPipelineHash point_pipeline_hash = pipeline_hash;
            point_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::PointList);
            point_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            point_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            point_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

            RHIPipeline* point_pipeline = shader_library.GetPipeline(point_pipeline_hash);
            if (point_pipeline)
            {
                command_list.SetGraphicsPipeline(*point_pipeline);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);

                for (const auto& renderable : gpu_scene.point_renderables)
                {
                    command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_offset, renderable.index_count * sizeof(uint32));
                    command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                    command_list.PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                    command_list.DrawIndexed(renderable.index_count, 1, 0, 0, 0);
                }
            }
        }

        if (pass == RenderPassType::DecalPass && (flags & DrawScene_Decal) != 0 && !gpu_scene.shader_decals.empty()
            && gpu_scene.decal_buffer.srv.IsValid() && view.render_targets.depth_srv.IsValid())
        {
            GraphicsPipelineHash decal_pipeline_hash = {};
            decal_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::DecalPass);
            decal_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            decal_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            decal_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            decal_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
            decal_pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(resource::MaterialBlendMode::Transparent);
            RHIPipeline* decal_pipeline = shader_library.GetPipeline(decal_pipeline_hash);
            if (decal_pipeline)
            {
                command_list.SetGraphicsPipeline(*decal_pipeline);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                command_list.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

                for (uint32 decal_index = 0; decal_index < static_cast<uint32>(gpu_scene.shader_decals.size()); ++decal_index)
                {
                    DecalPushConstants decal_push = {};
                    decal_push.Init();
                    decal_push.decal_buffer = static_cast<uint32>(gpu_scene.decal_buffer.srv.descriptor_index);
                    decal_push.decal_index = decal_index;
                    decal_push.depth_descriptor = static_cast<uint32>(view.render_targets.depth_srv.descriptor_index);
                    command_list.PushConstants(RHIShaderStage::Vertex, &decal_push, sizeof(DecalPushConstants), 0);
                    command_list.Draw(36, 1, 0, 0);
                }
            }
        }

        if (pass == RenderPassType::Sprite3DPass && (flags & DrawScene_3DSprite) != 0 && !gpu_scene.sprite_3d_renderables.empty())
        {
            GraphicsPipelineHash base_sprite_hash = {};
            base_sprite_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite3DPass);
            base_sprite_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            base_sprite_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            base_sprite_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            base_sprite_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

            command_list.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            GraphicsPipelineHash active_hash = {};
            bool has_active_pipeline = false;
            for (uint32 idx : view.sorted_sprite_3d_indices)
            {
                const Sprite3DRenderable& renderable = gpu_scene.sprite_3d_renderables[idx];
                if (renderable.IsParticle() && (view.show_flags & Show_Particles) == 0)
                {
                    continue;
                }
                const Sprite3DPassMode pass_mode = renderable.IsText() ? Sprite3DPassMode::Text
                    : (renderable.IsParticle() ? Sprite3DPassMode::Particle : Sprite3DPassMode::Sprite);

                GraphicsPipelineHash renderable_hash = base_sprite_hash;
                renderable_hash.storage.bits.pass_mode = static_cast<uint64>(pass_mode);
                renderable_hash.storage.bits.blend_mode = pass_mode == Sprite3DPassMode::Text ? static_cast<uint64>(resource::MaterialBlendMode::Transparent) : static_cast<uint64>(renderable.blend_mode);

                if (!has_active_pipeline || !(active_hash == renderable_hash))
                {
                    RHIPipeline* pipeline = shader_library.GetPipeline(renderable_hash);
                    if (!pipeline)
                    {
                        has_active_pipeline = false;
                        continue;
                    }
                    active_hash = renderable_hash;
                    has_active_pipeline = true;
                    command_list.SetGraphicsPipeline(*pipeline);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                }

                if (!renderable.IsText())
                {
                    SpritePushConstants push_constants = {};
                    push_constants.Init();
                    push_constants.size_pivot = { renderable.size.x, renderable.size.y, renderable.pivot.x, renderable.pivot.y };
                    push_constants.uv_rect = renderable.uv_rect;
                    push_constants.instance_index = renderable.instance_index;
                    push_constants.material_index = renderable.material_index;
                    if (renderable.IsBillboard())
                    {
                        push_constants.flags |= SHADER_SPRITE_FLAG_BILLBOARD;
                    }
                    if (renderable.IsParticle())
                    {
                        push_constants.flags |= SHADER_SPRITE_FLAG_PARTICLE;
                        push_constants.SetResourceIndex(static_cast<uint32>(gpu_scene.particle_buffer.srv.descriptor_index));
                    }
                    command_list.PushConstants(RHIShaderStage::Vertex, &push_constants, sizeof(SpritePushConstants), 0);
                    command_list.Draw(6, 1, 0, 0);
                }
                else
                {
                    if (!renderable.font || !utils::CreateRenderData(*device, *renderable.font) || !renderable.font->render_data.IsValid())
                    {
                        continue;
                    }
                    if (renderable.size.x <= 0.0f || renderable.size.y <= 0.0f)
                    {
                        continue;
                    }
                    SpritePushConstants push_constants = {};
                    push_constants.Init();
                    push_constants.size_pivot = { renderable.size.x, renderable.size.y, renderable.pivot.x, renderable.pivot.y };
                    push_constants.uv_rect = renderable.uv_rect;
                    push_constants.instance_index = renderable.instance_index;
                    if (renderable.IsBillboard())
                    {
                        push_constants.flags |= SHADER_SPRITE_FLAG_BILLBOARD;
                    }
                    push_constants.material_index = renderable.material_index;
                    push_constants.SetResourceIndex(static_cast<uint32>(renderable.font->render_data.atlas_srv.descriptor_index));
                    command_list.PushConstants(RHIShaderStage::Vertex, &push_constants, sizeof(SpritePushConstants), 0);
                    command_list.Draw(6, 1, 0, 0);
                }
            }
        }

        if (pass == RenderPassType::Sprite2DPass && (flags & DrawScene_2DSprite) != 0 && !gpu_scene.sprite_2d_renderables.empty())
        {
            GraphicsPipelineHash sprite_2d_pipeline_hash = {};
            sprite_2d_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite2DPass);
            sprite_2d_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            sprite_2d_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            sprite_2d_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            sprite_2d_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
            sprite_2d_pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite2DPassMode::Sprite);

            GraphicsPipelineHash text_2d_pipeline_hash = sprite_2d_pipeline_hash;
            text_2d_pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite2DPassMode::Text);

            RHIPipeline* sprite_2d_pipeline = shader_library.GetPipeline(sprite_2d_pipeline_hash);
            RHIPipeline* text_2d_pipeline = shader_library.GetPipeline(text_2d_pipeline_hash);
            if (!sprite_2d_pipeline || !text_2d_pipeline)
            {
                return false;
            }

            const float2 viewport_size = { static_cast<float>(view.viewport.width), static_cast<float>(view.viewport.height) };
            auto pack_sprite_2d_position = [&](const float2& anchor, const float2& position)
            {
                const float2 pixel_position = { anchor.x * viewport_size.x + position.x, anchor.y * viewport_size.y + position.y };
                const float normalized_x = viewport_size.x > 0.0f ? pixel_position.x / viewport_size.x : 0.0f;
                const float normalized_y = viewport_size.y > 0.0f ? pixel_position.y / viewport_size.y : 0.0f;
                return static_cast<uint32>(XMConvertFloatToHalf(normalized_x)) | (static_cast<uint32>(XMConvertFloatToHalf(normalized_y)) << 16);
            };

            command_list.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            bool active_is_text_2d = false;
            bool has_active_pipeline = false;
            for (uint32 idx : view.sorted_sprite_2d_indices)
            {
                const Sprite2DRenderable& renderable = gpu_scene.sprite_2d_renderables[idx];
                float2 ui_scale = { 1.0f, 1.0f };
                if (renderable.reference_resolution.x > 0.0f && renderable.reference_resolution.y > 0.0f)
                {
                    const float s = std::pow(viewport_size.x / renderable.reference_resolution.x, 1.0f - renderable.match) * std::pow(viewport_size.y / renderable.reference_resolution.y, renderable.match);
                    ui_scale = { s, s };
                }
                const float2 scaled_size = { renderable.size.x * ui_scale.x, renderable.size.y * ui_scale.y };
                const float2 scaled_position = { renderable.position.x * ui_scale.x, renderable.position.y * ui_scale.y };
                if (!has_active_pipeline || active_is_text_2d != renderable.IsText())
                {
                    active_is_text_2d = renderable.IsText();
                    has_active_pipeline = true;
                    command_list.SetGraphicsPipeline(renderable.IsText() ? *text_2d_pipeline : *sprite_2d_pipeline);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                }

                if (!renderable.IsText())
                {
                    SpritePushConstants push_constants = {};
                    push_constants.Init();
                    push_constants.size_pivot = { scaled_size.x, scaled_size.y, renderable.pivot.x, renderable.pivot.y };
                    push_constants.uv_rect = renderable.uv_rect;
                    push_constants.instance_index = pack_sprite_2d_position(renderable.anchor, scaled_position);
                    push_constants.material_index = renderable.material_index;
                    command_list.PushConstants(RHIShaderStage::Vertex, &push_constants, sizeof(SpritePushConstants), 0);
                    command_list.Draw(6, 1, 0, 0);
                }
                else
                {
                    if (!renderable.font || !utils::CreateRenderData(*device, *renderable.font) || !renderable.font->render_data.IsValid())
                    {
                        continue;
                    }
                    if (renderable.size.x <= 0.0f || renderable.size.y <= 0.0f)
                    {
                        continue;
                    }
                    SpritePushConstants push_constants = {};
                    push_constants.Init();
                    push_constants.size_pivot = { scaled_size.x, scaled_size.y, renderable.pivot.x, renderable.pivot.y };
                    push_constants.uv_rect = renderable.uv_rect;
                    push_constants.instance_index = pack_sprite_2d_position(renderable.anchor, scaled_position);
                    push_constants.material_index = renderable.material_index;
                    push_constants.SetResourceIndex(static_cast<uint32>(renderable.font->render_data.atlas_srv.descriptor_index));
                    command_list.PushConstants(RHIShaderStage::Vertex, &push_constants, sizeof(SpritePushConstants), 0);
                    command_list.Draw(6, 1, 0, 0);
                }
            }
        }

        return true;
    }

    void RendererInternal::Initialize(const RendererDesc& desc)
    {
        device = desc.device;
        shader_compiler_options.shader_bin_root_path = desc.shader_bin_root_path;
        clear_color = desc.clear_color;
        vsync_enabled = desc.vsync_enabled;
        vsync_requested = desc.vsync_enabled;

        for (uint32 i = 0; i < max_frames_in_flight; ++i)
        {
            FrameContext& frame_context = frame_contexts[i];
            frame_context.fence = device->CreateFence(0);
            if (!frame_context.fence)
            {
                backlog::Post("failed to create frame contexts", backlog::LogLevel::Error);
                return;
            }
        }

        enqueued_work_command_allocator = device->CreateCommandAllocator(RHIQueueType::Graphics);
        enqueued_work_command_list = device->CreateCommandList(RHIQueueType::Graphics);
        enqueued_work_fence = device->CreateFence(0);
        if (!enqueued_work_command_allocator || !enqueued_work_command_list || !enqueued_work_fence)
        {
            backlog::Post("failed to create enqueued rendering work command objects", backlog::LogLevel::Error);
            enqueued_work_command_allocator = nullptr;
            enqueued_work_command_list = nullptr;
            enqueued_work_fence = nullptr;
            return;
        }

        RHIBufferDesc shader_frame_buffer_desc = {};
        shader_frame_buffer_desc.size = sizeof(ShaderFrame);
        shader_frame_buffer_desc.usage = RHIResourceUsage::Default;
        shader_frame_buffer_desc.bind_flags = RHIBindFlags::ConstantBuffer;
        shader_frame_buffer = device->CreateBuffer(shader_frame_buffer_desc);
        if (!shader_frame_buffer)
        {
            backlog::Post("failed to create shader frame buffer", backlog::LogLevel::Error);
            return;
        }
        shader_frame_buffer->SetName("Shader Frame Buffer");

        RHISubresourceDesc shader_frame_subresource_desc = {};
        shader_frame_subresource_desc.type = RHISubresourceType::ConstantBuffer;
        shader_frame_subresource_desc.buffer_offset = 0;
        shader_frame_subresource_desc.buffer_size = sizeof(ShaderFrame);
        if (!device->CreateSubresource(*shader_frame_buffer, shader_frame_subresource_desc, &shader_frame_buffer_cbv))
        {
            backlog::Post("failed to create shader frame subresource", backlog::LogLevel::Error);
            shader_frame_buffer = nullptr;
            return;
        }

        current_frame_slot = 0;
        ReloadShaders();
    }

    bool RendererInternal::ReloadShaders()
    {
        if (!device)
        {
            return false;
        }

        WaitIdle();
        shader_library = resource::ShaderLibrary(device, shader_compiler_options);
        if (!shader_library.LoadManifest(resource::GetDefaultShaderManifest()))
        {
            return false;
        }

        return shader_library.BuildAllPipelines(HDR_COLOR_BUFFER_FORMAT, RENDERTARGET_BUFFER_FORMAT, DEPTH_BUFFER_FORMAT, 1u);
    }

    RHIShader* RendererInternal::GetShader(resource::ShaderId shader_id) const
    {
        return shader_library.GetShader(shader_id);
    }

    void RendererInternal::BeginFrame(platform::Window& window, float delta_time)
    {
        frame_time_seconds += delta_time;

        if (current_window != &window)
        {
            WaitIdle();
            back_buffers_rtv = {};
        }

        current_window = &window;
        const uint32 frame_slot = current_frame_slot;
        FrameContext& frame_context = GetFrameContext();

        enqueued_work_recorded = false;
        if (enqueued_work_fence_value > 0 && enqueued_work_fence->GetCompletedValue() >= enqueued_work_fence_value)
        {
            enqueued_work_fence_value = 0;
        }
        if (enqueued_work_fence_value == 0)
        {
            enqueued_work_scratch_resources.clear();
            enqueued_work_succeeded = true;
            enqueued_work_recorded = true;
            jobsystem::Execute(GetRenderingWorkContext(), [this](jobsystem::JobArgs args) {
                enqueued_work_command_allocator->Reset();
                enqueued_work_command_list->Begin(*enqueued_work_command_allocator);
                enqueued_work_succeeded = utils::FlushEnqueuedRenderingWork(*device, *this, *enqueued_work_command_list, enqueued_work_scratch_resources);
            });
        }

        // wait for fence here
        frame_context.BeginFrame();

        RHICommandList* command_list = frame_context.BeginCommandList(*device);
        profiler::BeginFrameGPU(*device, frame_slot, *command_list);

        device->BeginFrame(frame_slot);

        {
            auto cpu_range = profiler::ScopedRangeCPU("Flush Resource Uploads");
            utils::FlushEnqueuedResourceUploads(*device, static_cast<uint32>(r_upload_budget.GetInt()));
        }

        CreateBackBufferSubresources();

        RHISubresourceBinding back_buffer_binding = {};
        if (!GetCurrentBackBufferBinding(back_buffer_binding))
        {
            return;
        }

        command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::RenderTarget);
        command_list->ClearRenderTarget(back_buffer_binding, clear_color);
    }

    void RendererInternal::OnResize(platform::Window& window, uint32 width, uint32 height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }

        RHISwapchain* swapchain = window.GetRHISwapchain();
        if (!swapchain)
        {
            return;
        }

        WaitIdle();

        back_buffers_rtv = {};

        if (!swapchain->Resize(width, height))
        {
            backlog::Post("failed to resize swapchain", backlog::LogLevel::Error);
        }
    }

    void RendererInternal::UpdateDDGIProbe(FrameContext& frame_context, View& view, RHICommandList& command_list)
    {
        GPUScene& gpu_scene = view.scene->GetGPUScene();
        const ShaderDDGIVolume& ddgi_volume = gpu_scene.shader_ddgi_volume;
        const ShaderEnvironment& environment_lighting = gpu_scene.shader_environment;
        RHIPipeline* ddgi_probe_update_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSDDGIProbeUpdate));

        const uint32 probe_update_start = ddgi_volume.total_probe_count > 0 ? gpu_scene.ddgi.probe_update_offset % ddgi_volume.total_probe_count : 0;
        const uint32 probes_per_frame = gpu_scene.ddgi.history_valid ? (std::min)(ddgi_volume.probes_per_frame, ddgi_volume.total_probe_count) : ddgi_volume.total_probe_count;
        const uint32 probe_update_dispatch_width = (std::min)(probes_per_frame, 65535u);

        if (environment_lighting.diffuse_gi_mode != SHADER_DIFFUSE_GI_MODE_DDGI ||
            (ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) == 0 ||
            ddgi_volume.probe_counts.x == 0 ||
            ddgi_volume.probe_counts.y == 0 ||
            ddgi_volume.probe_counts.z == 0 ||
            ddgi_volume.total_probe_count == 0 ||
            probes_per_frame == 0 ||
            !ddgi_probe_update_pipeline ||
            !gpu_scene.ddgi.irradiance_texture ||
            !gpu_scene.ddgi.irradiance_history_texture ||
            !gpu_scene.ddgi.visibility_texture ||
            !gpu_scene.ddgi.visibility_history_texture ||
            !gpu_scene.ddgi.probe_data_buffer ||
            !gpu_scene.ddgi.probe_data_history_buffer ||
            !gpu_scene.ddgi.irradiance_texture_srv.IsValid() ||
            !gpu_scene.ddgi.irradiance_texture_uav.IsValid() ||
            !gpu_scene.ddgi.irradiance_history_texture_srv.IsValid() ||
            !gpu_scene.ddgi.visibility_texture_srv.IsValid() ||
            !gpu_scene.ddgi.visibility_texture_uav.IsValid() ||
            !gpu_scene.ddgi.visibility_history_texture_srv.IsValid() ||
            !gpu_scene.ddgi.probe_data_buffer_srv.IsValid() ||
            !gpu_scene.ddgi.probe_data_buffer_uav.IsValid() ||
            !gpu_scene.ddgi.probe_data_history_buffer_srv.IsValid())
        {
            return;
        }

        const uint32 dispatch_width = (std::max)(probe_update_dispatch_width, 1u);
        const uint3 dispatch_groups = {
            dispatch_width,
            (probes_per_frame + dispatch_width - 1) / dispatch_width,
            1
        };

        auto gpu_range = profiler::ScopedRangeGPU("Update DDGI Probes", command_list);
        command_list.BeginEvent("Update DDGI Probes");
        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_texture, RHIResourceState::ShaderWrite);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_texture, RHIResourceState::ShaderWrite);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_buffer, RHIResourceState::ShaderWrite);
        if (gpu_scene.ddgi.history_valid)
        {
            command_list.TransitionResource(*gpu_scene.ddgi.irradiance_history_texture, RHIResourceState::ShaderRead);
            command_list.TransitionResource(*gpu_scene.ddgi.visibility_history_texture, RHIResourceState::ShaderRead);
            command_list.TransitionResource(*gpu_scene.ddgi.probe_data_history_buffer, RHIResourceState::ShaderRead);
        }

        command_list.SetComputePipeline(*ddgi_probe_update_pipeline);
        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_cbv;
        RHISubresourceBinding shader_view_binding = {};
        shader_view_binding.resource = view.view_constants.buffer.get();
        shader_view_binding.subresource = view.view_constants.cbv;
        command_list.SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);
        command_list.SetConstantBuffer(RHIShaderStage::Compute, 1, shader_view_binding);
        command_list.Dispatch(dispatch_groups.x, dispatch_groups.y, dispatch_groups.z);

        command_list.UAVBarrier(*gpu_scene.ddgi.irradiance_texture);
        command_list.UAVBarrier(*gpu_scene.ddgi.visibility_texture);
        command_list.UAVBarrier(*gpu_scene.ddgi.probe_data_buffer);

        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_texture, RHIResourceState::CopySource);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_texture, RHIResourceState::CopySource);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_buffer, RHIResourceState::CopySource);
        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_history_texture, RHIResourceState::CopyDest);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_history_texture, RHIResourceState::CopyDest);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_history_buffer, RHIResourceState::CopyDest);

        command_list.CopyResource(*gpu_scene.ddgi.irradiance_history_texture, *gpu_scene.ddgi.irradiance_texture);
        command_list.CopyResource(*gpu_scene.ddgi.visibility_history_texture, *gpu_scene.ddgi.visibility_texture);
        command_list.CopyResource(*gpu_scene.ddgi.probe_data_history_buffer, *gpu_scene.ddgi.probe_data_buffer);
        if (view.ddgi_debug_resources.probe_data_readback_buffer)
        {
            command_list.CopyBuffer(*view.ddgi_debug_resources.probe_data_readback_buffer, 0, *gpu_scene.ddgi.probe_data_buffer, 0, gpu_scene.ddgi.probe_data_buffer->GetDesc().buffer_desc.size);
        }

        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_buffer, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_history_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_history_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_history_buffer, RHIResourceState::ShaderRead);
        command_list.EndEvent();

        gpu_scene.ddgi.probe_update_offset = (probe_update_start + probes_per_frame) % ddgi_volume.total_probe_count;
        gpu_scene.ddgi.history_valid = true;
        view.ddgi_debug_resources.probe_data_readback_valid = view.ddgi_debug_resources.probe_data_readback_buffer != nullptr;
    }

#ifndef WON_SHIPPING
    void RendererInternal::BuildDebug3D(const View& view)
    {
        GPUScene& gpu_scene = view.scene->GetGPUScene();

        if ((view.show_flags & Show_Colliders) != 0)
        {
            auto collider_array = view.scene->GetComponentArray<ecs::Collider3DComponent>().get();
            auto transform_array = view.scene->GetComponentArray<ecs::TransformComponent>().get();
            if (collider_array && transform_array)
            {
                for (Size collider_index = 0; collider_index < collider_array->GetSize(); ++collider_index)
                {
                    const ecs::Entity entity = collider_array->index_to_entity[collider_index];
                    if (!transform_array->HasData(entity))
                    {
                        continue;
                    }

                    const ecs::Collider3DComponent& collider = collider_array->data[collider_index];
                    if (!collider.IsEnabled())
                    {
                        continue;
                    }

                    const DirectX::XMMATRIX world = transform_array->GetData(entity).GetWorldTransform();
                    const uint32 color = collider.IsTrigger() ? debugdraw::color::collider_trigger : debugdraw::color::collider;
                    if (collider.shape_type == ecs::Collider3DComponent::ShapeType::Sphere)
                    {
                        const DirectX::XMVECTOR center = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&collider.offset), world);
                        const float scale_x = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[0]));
                        const float scale_y = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[1]));
                        const float scale_z = DirectX::XMVectorGetX(DirectX::XMVector3Length(world.r[2]));
                        const float max_scale = (std::max)((std::max)(scale_x, scale_y), scale_z);
                        float3 world_center = {};
                        DirectX::XMStoreFloat3(&world_center, center);
                        debugdraw::Sphere3D(world_center, (std::max)(0.0f, collider.radius) * max_scale, color);
                        continue;
                    }

                    math::AABB world_bounds = {};
                    if (collider.shape_type == ecs::Collider3DComponent::ShapeType::HeightField)
                    {
                        const ecs::GeometryComponent* geometry = view.scene->GetComponent<ecs::GeometryComponent>(entity);
                        if (!geometry || !geometry->local_bounds.IsValid())
                        {
                            continue;
                        }
                        world_bounds = geometry->local_bounds.TransformAABB(world);
                    }
                    else
                    {
                        const float3 half_extent = {
                            (std::max)(0.0f, collider.half_extent.x),
                            (std::max)(0.0f, collider.half_extent.y),
                            (std::max)(0.0f, collider.half_extent.z)
                        };
                        math::AABB local_bounds = {};
                        local_bounds.CreateFromHalfWidth(collider.offset, half_extent);
                        world_bounds = local_bounds.TransformAABB(world);
                    }

                    if (world_bounds.IsValid())
                    {
                        debugdraw::Box3D(world_bounds.min, world_bounds.max, color);
                    }
                }
            }
        }

        if ((view.show_flags & Show_BVH) != 0)
        {
            const math::bvh::BVH& cpu_bvh = view.scene->GetSceneBVH();
            for (const math::bvh::BVHNode& node : cpu_bvh.nodes)
            {
                debugdraw::Box3D(node.bounds.min, node.bounds.max, node.IsLeaf() ? debugdraw::color::bvh_cpu_leaf : debugdraw::color::bvh_cpu_internal);
            }

            for (const ShaderBVHNode& node : gpu_scene.shader_bvh_nodes)
            {
                debugdraw::Box3D(node.bounds_min, node.bounds_max, node.primitive_count > 0 ? debugdraw::color::bvh_gpu_leaf : debugdraw::color::bvh_gpu_internal);
            }
        }

        if ((view.show_flags & Show_DDGI) != 0 && (gpu_scene.shader_ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) != 0)
        {
            const ShaderDDGIVolume& ddgi_volume = gpu_scene.shader_ddgi_volume;
            const float3 probe_span = {
                static_cast<float>((ddgi_volume.probe_counts.x > 0 ? ddgi_volume.probe_counts.x - 1 : 0)) * ddgi_volume.probe_spacing.x,
                static_cast<float>((ddgi_volume.probe_counts.y > 0 ? ddgi_volume.probe_counts.y - 1 : 0)) * ddgi_volume.probe_spacing.y,
                static_cast<float>((ddgi_volume.probe_counts.z > 0 ? ddgi_volume.probe_counts.z - 1 : 0)) * ddgi_volume.probe_spacing.z
            };
            const float3 volume_max = {
                ddgi_volume.volume_min.x + probe_span.x,
                ddgi_volume.volume_min.y + probe_span.y,
                ddgi_volume.volume_min.z + probe_span.z
            };
            debugdraw::Box3D(ddgi_volume.volume_min, volume_max, debugdraw::color::ddgi_volume);

            if (view.ddgi_debug_resources.probe_data_readback_valid && view.ddgi_debug_resources.probe_data_readback_buffer && view.ddgi_debug_resources.probe_data_readback_buffer->GetMappedData())
            {
                const float min_probe_spacing = (std::min)(ddgi_volume.probe_spacing.x, (std::min)(ddgi_volume.probe_spacing.y, ddgi_volume.probe_spacing.z));
                const float probe_marker_size = (std::max)(0.05f, min_probe_spacing * 0.2f);

                const uint32 max_debug_probe_count = 4096;
                const uint32 total_probe_count = ddgi_volume.total_probe_count;
                const float sample_ratio = total_probe_count > max_debug_probe_count ? static_cast<float>(total_probe_count) / static_cast<float>(max_debug_probe_count) : 1.0f;
                const uint32 sampling_step = sample_ratio > 1.0f ? static_cast<uint32>((std::max)(1.0f, std::ceil(std::cbrt(sample_ratio)))) : 1u;
                const float4* probe_data = static_cast<const float4*>(view.ddgi_debug_resources.probe_data_readback_buffer->GetMappedData());
                const Size readback_probe_count = view.ddgi_debug_resources.probe_data_readback_buffer->GetDesc().buffer_desc.size / sizeof(float4);

                for (uint32 z = 0; z < ddgi_volume.probe_counts.z; z += sampling_step)
                {
                    for (uint32 y = 0; y < ddgi_volume.probe_counts.y; y += sampling_step)
                    {
                        for (uint32 x = 0; x < ddgi_volume.probe_counts.x; x += sampling_step)
                        {
                            const uint32 probe_linear_index = x + y * ddgi_volume.probe_counts.x + z * ddgi_volume.probe_counts.x * ddgi_volume.probe_counts.y;
                            if (probe_linear_index >= readback_probe_count)
                            {
                                continue;
                            }

                            const float4 data = probe_data[probe_linear_index];
                            const float3 position = {
                                ddgi_volume.volume_min.x + static_cast<float>(x) * ddgi_volume.probe_spacing.x + data.x,
                                ddgi_volume.volume_min.y + static_cast<float>(y) * ddgi_volume.probe_spacing.y + data.y,
                                ddgi_volume.volume_min.z + static_cast<float>(z) * ddgi_volume.probe_spacing.z + data.z
                            };
                            const float relocation = std::sqrt(data.x * data.x + data.y * data.y + data.z * data.z);
                            uint32 color = debugdraw::color::ddgi_probe;
                            if (data.w < 0.5f)
                            {
                                color = debugdraw::color::ddgi_probe_invalid;
                            }
                            else if (relocation > 0.01f)
                            {
                                color = debugdraw::color::ddgi_probe_relocated;
                            }
                            debugdraw::Cross3D(position, probe_marker_size, color);
                        }
                    }
                }
            }
        }
    }

    void RendererInternal::DrawDebug3D(const View& view, RHICommandList& command_list)
    {
        const Vector<debugdraw::Item3D>& line_vertices = debugdraw::GetItems3D();
        RHISubresourceBinding back_buffer_binding = {};
        if (line_vertices.empty() || !GetCurrentBackBufferBinding(back_buffer_binding))
        {
            debugdraw::Clear3D();
            return;
        }

        FrameContext& frame_context = GetFrameContext();

        GraphicsPipelineHash debug_3d_pipeline_hash = {};
        debug_3d_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::DebugDraw3DPass);
        debug_3d_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::LineList);
        debug_3d_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        debug_3d_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        debug_3d_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        RHIPipeline* debug_3d_pipeline = shader_library.GetPipeline(debug_3d_pipeline_hash);
        if (!debug_3d_pipeline)
        {
            debugdraw::Clear3D();
            return;
        }

        const Size required_buffer_size = line_vertices.size() * sizeof(debugdraw::Item3D);
        Size current_buffer_size = 0;
        if (debug_3d_buffer)
        {
            current_buffer_size = debug_3d_buffer->GetDesc().buffer_desc.size;
        }
        if (!debug_3d_buffer || current_buffer_size < required_buffer_size)
        {
            frame_context.RemoveResourceDeferred(debug_3d_buffer);
            RHIBufferDesc buffer_desc = {};
            buffer_desc.size = required_buffer_size;
            buffer_desc.usage = RHIResourceUsage::Default;
            buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
            debug_3d_buffer = device->CreateBuffer(buffer_desc);
            if (!debug_3d_buffer)
            {
                debugdraw::Clear3D();
                return;
            }
            debug_3d_buffer->SetName("DebugDraw3D Buffer");

            debug_3d_buffer_srv = {};
            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.buffer_offset = 0;
            srv_desc.buffer_size = debug_3d_buffer->GetDesc().buffer_desc.size;
            srv_desc.buffer_stride = sizeof(debugdraw::Item3D);
            if (!device->CreateSubresource(*debug_3d_buffer, srv_desc, &debug_3d_buffer_srv))
            {
                debug_3d_buffer = nullptr;
                debugdraw::Clear3D();
                return;
            }
        }

        if (!UpdateDefaultBuffer(frame_context, *debug_3d_buffer, line_vertices.data(), required_buffer_size, RHIResourceState::ShaderRead, 0, command_list))
        {
            debugdraw::Clear3D();
            return;
        }

        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_cbv;
        RHISubresourceBinding shader_view_binding = {};
        shader_view_binding.resource = view.view_constants.buffer.get();
        shader_view_binding.subresource = view.view_constants.cbv;

        command_list.SetGraphicsPipeline(*debug_3d_pipeline);
        command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
        command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
        command_list.SetPrimitiveTopology(RHIPrimitiveTopology::LineList);

        DebugDraw3DPushConstants push = {};
        push.Init();
        push.vertex_buffer = static_cast<uint32>(debug_3d_buffer_srv.descriptor_index);
        command_list.PushConstants(RHIShaderStage::Vertex, &push, sizeof(DebugDraw3DPushConstants), 0);
        command_list.Draw(static_cast<uint32>(line_vertices.size()), 1, 0, 0);
        debugdraw::Clear3D();
    }
#endif

#ifndef WON_SHIPPING
    void RendererInternal::DrawDebug2D(RHICommandList& command_list)
    {
        const Vector<debugdraw::Item2D>& items = debugdraw::GetItems2D();
        RHISubresourceBinding back_buffer_binding = {};
        if (items.empty() || !builtinfont::IsReady() || !GetCurrentBackBufferBinding(back_buffer_binding))
        {
            debugdraw::Clear2D();
            return;
        }

        GraphicsPipelineHash debug_2d_pipeline_hash = {};
        debug_2d_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::DebugDraw2DPass);
        debug_2d_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        debug_2d_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        debug_2d_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        debug_2d_pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(resource::MaterialBlendMode::Transparent);
        RHIPipeline* debug_2d_pipeline = shader_library.GetPipeline(debug_2d_pipeline_hash);
        if (!debug_2d_pipeline)
        {
            debugdraw::Clear2D();
            return;
        }

        const float bb_width = static_cast<float>(back_buffer_binding.resource->GetDesc().texture_desc.width);
        const float bb_height = static_cast<float>(back_buffer_binding.resource->GetDesc().texture_desc.height);
        if (bb_width <= 0.0f || bb_height <= 0.0f)
        {
            debugdraw::Clear2D();
            return;
        }

        RHIViewport viewport = {};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = bb_width;
        viewport.height = bb_height;
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;

        RHIRect scissor = {};
        scissor.x = 0;
        scissor.y = 0;
        scissor.width = back_buffer_binding.resource->GetDesc().texture_desc.width;
        scissor.height = back_buffer_binding.resource->GetDesc().texture_desc.height;

        command_list.TransitionResource(*back_buffer_binding.resource, RHIResourceState::RenderTarget);
        command_list.SetRenderTargets({ back_buffer_binding }, nullptr);
        command_list.SetViewport(viewport);
        command_list.SetScissor(scissor);
        command_list.SetGraphicsPipeline(*debug_2d_pipeline);
        command_list.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

        const uint32 atlas_index = static_cast<uint32>(builtinfont::GetAtlasSRV().descriptor_index);
        const float cell_u = static_cast<float>(builtinfont::glyph_width) / static_cast<float>(builtinfont::atlas_width);
        const float cell_v = static_cast<float>(builtinfont::glyph_height) / static_cast<float>(builtinfont::atlas_height);

        for (const debugdraw::Item2D& item : items)
        {
            if (item.is_rect)
            {
                DebugDraw2DPushConstants push = {};
                push.Init();
                push.rect = { item.position.x / bb_width, item.position.y / bb_height, item.size.x / bb_width, item.size.y / bb_height };
                push.color = item.color;
                push.atlas_index = 0xffffffffu;
                command_list.PushConstants(RHIShaderStage::Vertex, &push, sizeof(push), 0);
                command_list.Draw(6, 1, 0, 0);
                continue;
            }

            const float glyph_w = static_cast<float>(builtinfont::glyph_width) * item.scale;
            const float glyph_h = static_cast<float>(builtinfont::glyph_height) * item.scale;
            float pen_x = item.position.x;
            float pen_y = item.position.y;
            for (unsigned char ch : item.text)
            {
                if (ch == '\n')
                {
                    pen_x = item.position.x;
                    pen_y += glyph_h;
                    continue;
                }

                const int col = ch % builtinfont::atlas_cols;
                const int row = ch / builtinfont::atlas_cols;

                DebugDraw2DPushConstants push = {};
                push.Init();
                push.rect = { pen_x / bb_width, pen_y / bb_height, glyph_w / bb_width, glyph_h / bb_height };
                push.uv_rect = { col * cell_u, row * cell_v, (col + 1) * cell_u, (row + 1) * cell_v };
                push.color = item.color;
                push.atlas_index = atlas_index;
                command_list.PushConstants(RHIShaderStage::Vertex, &push, sizeof(push), 0);
                command_list.Draw(6, 1, 0, 0);

                pen_x += glyph_w;
            }
        }

        debugdraw::Clear2D();
    }
#endif

#ifndef WON_SHIPPING
    void RendererInternal::RenderDebug2D()
    {
        if (debugdraw::GetItems2D().empty())
        {
            return;
        }

        RHICommandList* command_list = GetFrameContext().BeginCommandList(*device);
        if (!command_list)
        {
            debugdraw::Clear2D();
            return;
        }

        jobsystem::Execute(GetRenderingWorkContext(), [this, command_list](jobsystem::JobArgs args)
        {
            auto gpu_range = profiler::ScopedRangeGPU("DebugDraw2D Pass", *command_list);
            command_list->BeginEvent("DebugDraw2D Pass");
            DrawDebug2D(*command_list);
            command_list->EndEvent();
        });
    }
#endif

    bool RendererInternal::Update(View& view)
    {
        if (!view.scene)
        {
            return false;
        }

        ecs::Scene& scene = *view.scene;
        GPUScene& gpu_scene = scene.GetGPUScene();

        FrameContext& frame_context = GetFrameContext();
        RHICommandList* command_list = frame_context.BeginCommandList(*device);
        if (!command_list)
        {
            return false;
        }

        if (scene.GetUpdateIndex() != gpu_scene.synced_index)
        {
            {
                auto cpu_range = profiler::ScopedRangeCPU("Build GPU BVH");
                scene.BuildGPUBVH();
            }
            gpu_scene.Update(scene, *device, *command_list, current_frame_slot);
            gpu_scene.synced_index = scene.GetUpdateIndex();
        }

        {
            auto cpu_range = profiler::ScopedRangeCPU("Build View Resources");
            auto gpu_range = profiler::ScopedRangeGPU("Build View Resources", *command_list);
            if (!BuildViewResources(frame_context, view, *command_list))
            {
                return false;
            }
        }

        if (!brdf_lut)
        {
            won::utils::Timer brdf_setup_timer;
            RHIPipeline* brdf_integration_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSBRDFIntegration));

            RHITextureDesc brdf_lut_desc = {};
            brdf_lut_desc.width = brdf_lut_resolution;
            brdf_lut_desc.height = brdf_lut_resolution;
            brdf_lut_desc.depth = 1;
            brdf_lut_desc.mip_levels = 1;
            brdf_lut_desc.array_layers = 1;
            brdf_lut_desc.sample_count = 1;
            brdf_lut_desc.format = RHIFormat::R16G16B16A16Float;
            brdf_lut_desc.usage = RHIResourceUsage::Default;
            brdf_lut_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
            brdf_lut = brdf_integration_pipeline ? device->CreateTexture(brdf_lut_desc) : nullptr;
            if (brdf_lut)
            {
                brdf_lut->SetName("BRDF LUT");

                RHISubresourceDesc brdf_lut_srv_desc = {};
                brdf_lut_srv_desc.type = RHISubresourceType::ShaderResource;
                brdf_lut_srv_desc.format = brdf_lut_desc.format;
                brdf_lut_srv_desc.first_mip = 0;
                brdf_lut_srv_desc.mip_count = 1;
                brdf_lut_srv_desc.first_slice = 0;
                brdf_lut_srv_desc.slice_count = 1;
                device->CreateSubresource(*brdf_lut, brdf_lut_srv_desc, &brdf_lut_srv);

                RHISubresourceDesc brdf_lut_uav_desc = {};
                brdf_lut_uav_desc.type = RHISubresourceType::UnorderedAccess;
                brdf_lut_uav_desc.format = brdf_lut_desc.format;
                brdf_lut_uav_desc.first_mip = 0;
                brdf_lut_uav_desc.mip_count = 1;
                brdf_lut_uav_desc.first_slice = 0;
                brdf_lut_uav_desc.slice_count = 1;
                device->CreateSubresource(*brdf_lut, brdf_lut_uav_desc, &brdf_lut_uav);

                auto gpu_range = profiler::ScopedRangeGPU("Integrate BRDF", *command_list);
                command_list->BeginEvent("Integrate BRDF");
                command_list->TransitionResource(*brdf_lut, RHIResourceState::ShaderWrite);
                command_list->SetComputePipeline(*brdf_integration_pipeline);
                BRDFIntegrationPushConstants brdf_push = {};
                brdf_push.Init();
                brdf_push.output_descriptor = static_cast<uint32>(brdf_lut_uav.descriptor_index);
                command_list->PushConstants(RHIShaderStage::Compute, &brdf_push, sizeof(brdf_push), 0);
                const uint32 brdf_group_count = (brdf_lut_resolution + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D;
                command_list->Dispatch(brdf_group_count, brdf_group_count, 1u);
                command_list->UAVBarrier(*brdf_lut);
                command_list->TransitionResource(*brdf_lut, RHIResourceState::ShaderRead);
                command_list->EndEvent();
                wonlog("[Startup] brdf lut bake dispatched (%ux%u, cpu setup %.1f ms; gpu time in profiler overlay)", brdf_lut_resolution, brdf_lut_resolution, brdf_setup_timer.ElapsedMilliSeconds());
            }
        }

        if (!ltc_matrix_lut || !ltc_fresnel_lut)
        {
            RHITextureDesc ltc_lut_desc = {};
            ltc_lut_desc.width = ltc_lut_resolution;
            ltc_lut_desc.height = ltc_lut_resolution;
            ltc_lut_desc.depth = 1;
            ltc_lut_desc.mip_levels = 1;
            ltc_lut_desc.array_layers = 1;
            ltc_lut_desc.sample_count = 1;
            ltc_lut_desc.format = RHIFormat::R16G16B16A16Float;
            ltc_lut_desc.usage = RHIResourceUsage::Default;
            ltc_lut_desc.bind_flags = RHIBindFlags::ShaderResource;

            Vector<uint16> ltc_matrix_half(ltc_lut_resolution * ltc_lut_resolution * 4);
            for (Size i = 0; i < ltc_matrix_half.size(); ++i)
            {
                ltc_matrix_half[i] = XMConvertFloatToHalf(ltc_matrix_lut_data[i]);
            }
            ltc_matrix_lut = device->CreateTexture(ltc_lut_desc, ltc_matrix_half.data(), ltc_matrix_half.size() * sizeof(uint16));
            if (ltc_matrix_lut)
            {
                ltc_matrix_lut->SetName("LTC Matrix LUT");

                RHISubresourceDesc ltc_matrix_srv_desc = {};
                ltc_matrix_srv_desc.type = RHISubresourceType::ShaderResource;
                ltc_matrix_srv_desc.format = ltc_lut_desc.format;
                ltc_matrix_srv_desc.first_mip = 0;
                ltc_matrix_srv_desc.mip_count = 1;
                ltc_matrix_srv_desc.first_slice = 0;
                ltc_matrix_srv_desc.slice_count = 1;
                device->CreateSubresource(*ltc_matrix_lut, ltc_matrix_srv_desc, &ltc_matrix_lut_srv);
            }

            Vector<uint16> ltc_fresnel_half(ltc_lut_resolution * ltc_lut_resolution * 4);
            for (Size i = 0; i < ltc_fresnel_half.size(); ++i)
            {
                ltc_fresnel_half[i] = XMConvertFloatToHalf(ltc_fresnel_lut_data[i]);
            }
            ltc_fresnel_lut = device->CreateTexture(ltc_lut_desc, ltc_fresnel_half.data(), ltc_fresnel_half.size() * sizeof(uint16));
            if (ltc_fresnel_lut)
            {
                ltc_fresnel_lut->SetName("LTC Fresnel LUT");

                RHISubresourceDesc ltc_fresnel_srv_desc = {};
                ltc_fresnel_srv_desc.type = RHISubresourceType::ShaderResource;
                ltc_fresnel_srv_desc.format = ltc_lut_desc.format;
                ltc_fresnel_srv_desc.first_mip = 0;
                ltc_fresnel_srv_desc.mip_count = 1;
                ltc_fresnel_srv_desc.first_slice = 0;
                ltc_fresnel_srv_desc.slice_count = 1;
                device->CreateSubresource(*ltc_fresnel_lut, ltc_fresnel_srv_desc, &ltc_fresnel_lut_srv);
            }
            wonlog("[Startup] ltc lut created: matrix=%d (srv=%d) fresnel=%d (srv=%d)",
                ltc_matrix_lut ? 1 : 0, (int)ltc_matrix_lut_srv.descriptor_index,
                ltc_fresnel_lut ? 1 : 0, (int)ltc_fresnel_lut_srv.descriptor_index);
        }

        {
            auto frame_gpu_range = profiler::ScopedRangeGPU("Update Frame Constants", *command_list);
            if (!UpdateFrameConstants(frame_context, view, *command_list))
            {
                return false;
            }
        }

        UpdateSkyCapture(gpu_scene, *command_list);
        return true;
    }

    void RendererInternal::UpdateSkyCapture(GPUScene& gpu_scene, RHICommandList& command_list)
    {
        GPUScene::SkyLightingResources& sky_lighting = gpu_scene.sky_lighting;
        const ShaderEnvironment& environment = gpu_scene.shader_environment;
        if (environment.sky_type == SHADER_SKY_TYPE_NONE
            || (environment.diffuse_gi_mode != SHADER_DIFFUSE_GI_MODE_SKY
                && environment.reflection_mode != SHADER_REFLECTION_MODE_SKY))
        {
            return;
        }

        ShaderEnvironment signature = environment;
        signature.diffuse_gi_mode = 0;
        signature.reflection_mode = 0;
        signature.irradiance_cubemap = -1;
        signature.specular_cubemap = -1;
        signature.specular_mip_count = 0.0f;
        signature.brdf_lut = -1;
        signature.ambient_color_ambient_intensity = {};
        signature.indirect_diffuse_specular_scale = {};

        const float3 current_sun = signature.sun_direction;
        signature.sun_direction = sky_lighting.signature.sun_direction;
        bool needs_capture = !sky_lighting.valid
            || std::memcmp(&signature, &sky_lighting.signature, sizeof(signature)) != 0;
        signature.sun_direction = current_sun;

        if (!needs_capture)
        {
            const XMVECTOR captured_direction = XMVector3Normalize(XMLoadFloat3(&sky_lighting.signature.sun_direction));
            const XMVECTOR current_direction = XMVector3Normalize(XMLoadFloat3(&current_sun));
            const float sun_cos = XMVectorGetX(XMVector3Dot(current_direction, captured_direction));
            needs_capture = sun_cos < std::cos(math::DegreesToRadians(sky_capture_sun_angle_threshold_degrees));
        }

        if (!needs_capture
            && sky_lighting.pending_irradiance_face >= static_cast<int32>(sky_cube_face_count)
            && sky_lighting.pending_specular_mip >= static_cast<int32>(sky_specular_mip_count))
        {
            return;
        }

        RHIPipeline* capture_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSSkyCapture));
        RHIPipeline* irradiance_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSIrradianceConvolve));
        RHIPipeline* prefilter_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSSpecularPrefilter));
        if (!capture_pipeline || !irradiance_pipeline || !prefilter_pipeline || !sky_lighting.capture_texture)
        {
            return;
        }

        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_cbv;

        auto gpu_range = profiler::ScopedRangeGPU("Capture Sky", command_list);
        command_list.BeginEvent("Capture Sky");

        if (needs_capture)
        {
            command_list.TransitionResource(*sky_lighting.capture_texture, RHIResourceState::ShaderWrite);
            command_list.SetComputePipeline(*capture_pipeline);
            command_list.SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);

            SkyCapturePushConstants capture_push = {};
            capture_push.Init();
            capture_push.output_descriptor = static_cast<uint32>(sky_lighting.capture_uav.descriptor_index);
            capture_push.face_resolution = sky_capture_resolution;
            capture_push.source_cubemap = static_cast<uint32>(environment.sky_cubemap);
            command_list.PushConstants(RHIShaderStage::Compute, &capture_push, sizeof(capture_push), 0);

            const uint32 capture_group_count = (sky_capture_resolution + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D;
            command_list.Dispatch(capture_group_count, capture_group_count, sky_cube_face_count);
            command_list.UAVBarrier(*sky_lighting.capture_texture);
            command_list.TransitionResource(*sky_lighting.capture_texture, RHIResourceState::ShaderRead);

            sky_lighting.bake_step = sky_lighting.valid ? 1u : sky_cube_face_count;
            sky_lighting.signature = signature;
            sky_lighting.pending_irradiance_face = sky_lighting.irradiance_texture ? 0 : static_cast<int32>(sky_cube_face_count);
            sky_lighting.pending_specular_mip = sky_lighting.specular_texture ? 0 : static_cast<int32>(sky_specular_mip_count);
        }

        if (sky_lighting.pending_irradiance_face < static_cast<int32>(sky_cube_face_count))
        {
            const uint32 face_offset = static_cast<uint32>(sky_lighting.pending_irradiance_face);
            const uint32 face_count = std::min(sky_lighting.bake_step, sky_cube_face_count - face_offset);

            command_list.TransitionResource(*sky_lighting.irradiance_texture, RHIResourceState::ShaderWrite);
            command_list.SetComputePipeline(*irradiance_pipeline);
            command_list.SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);

            SkyCapturePushConstants irradiance_push = {};
            irradiance_push.Init();
            irradiance_push.output_descriptor = static_cast<uint32>(sky_lighting.irradiance_uav.descriptor_index);
            irradiance_push.face_resolution = sky_irradiance_resolution;
            irradiance_push.source_cubemap = static_cast<uint32>(sky_lighting.capture_srv.descriptor_index);
            irradiance_push.face_offset = face_offset;
            command_list.PushConstants(RHIShaderStage::Compute, &irradiance_push, sizeof(irradiance_push), 0);

            const uint32 irradiance_group_count = (sky_irradiance_resolution + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D;
            command_list.Dispatch(irradiance_group_count, irradiance_group_count, face_count);
            command_list.UAVBarrier(*sky_lighting.irradiance_texture);
            command_list.TransitionResource(*sky_lighting.irradiance_texture, RHIResourceState::ShaderRead);

            sky_lighting.pending_irradiance_face = static_cast<int32>(face_offset + face_count);
        }

        if (sky_lighting.pending_specular_mip < static_cast<int32>(sky_specular_mip_count))
        {
            const uint32 first_mip = static_cast<uint32>(sky_lighting.pending_specular_mip);
            const uint32 mip_count = std::min(sky_lighting.bake_step, sky_specular_mip_count - first_mip);
            for (uint32 mip = first_mip; mip < first_mip + mip_count; ++mip)
            {
                const uint32 mip_resolution = sky_specular_resolution >> mip;

                command_list.TransitionResource(*sky_lighting.specular_texture, RHIResourceState::ShaderWrite);
                command_list.SetComputePipeline(*prefilter_pipeline);
                command_list.SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);

                SkyPrefilterPushConstants prefilter_push = {};
                prefilter_push.Init();
                prefilter_push.output_descriptor = static_cast<uint32>(sky_lighting.specular_mip_uav[mip].descriptor_index);
                prefilter_push.face_resolution = mip_resolution;
                prefilter_push.source_cubemap = static_cast<uint32>(sky_lighting.capture_srv.descriptor_index);
                prefilter_push.perceptual_roughness = static_cast<float>(mip) / static_cast<float>(sky_specular_mip_count - 1);
                prefilter_push.source_mip = static_cast<float>(mip) * 0.5f;
                command_list.PushConstants(RHIShaderStage::Compute, &prefilter_push, sizeof(prefilter_push), 0);

                const uint32 prefilter_group_count = (mip_resolution + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D;
                command_list.Dispatch(prefilter_group_count, prefilter_group_count, sky_cube_face_count);
                command_list.UAVBarrier(*sky_lighting.specular_texture);
                command_list.TransitionResource(*sky_lighting.specular_texture, RHIResourceState::ShaderRead);
            }

            sky_lighting.pending_specular_mip = static_cast<int32>(first_mip + mip_count);
        }

        command_list.EndEvent();
        sky_lighting.valid = true;
    }

    void RendererInternal::Render(View& view)
    {
        if (!Update(view))
        {
            return;
        }

        switch (view.render_path_type)
        {
        case RenderPathType::Forward:
		case RenderPathType::ForwardPlus: // ForwardPlus is handled in the same function as Forward, but with different internal logic
        default:
            {
                RenderForwardPath(view);
            }
            break;
        }
    }

    void RendererInternal::UpdateForwardLightList(View& view, RHICommandList& command_list)
    {
        View::LightResources& resources = view.light_resources;
        resources.forward_light_count = 0;

        if (!view.scene || view.camera_entity == ecs::INVALID_ENTITY)
        {
            return;
        }

        const ecs::CameraComponent* camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
        if (!camera_component)
        {
            return;
        }

        Vector<uint32> visible;
        rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();
        const uint32 light_count = static_cast<uint32>(gpu_scene.shader_lights.size());
        for (uint32 i = gpu_scene.directional_count; i < light_count; ++i)
        {
            if (gpu_scene.light_bounds[i].IntersectFrustum(camera_component->frustum))
            {
                visible.push_back(i);
                if (visible.size() >= NUM_MAX_LIGHTS_FORWARD_RENDERING)
                {
                    backlog::Post("Per-view forward light count reached the limit; remaining lights culled", backlog::LogLevel::Warning);
                    break;
                }
            }
        }

        if (visible.empty())
        {
            return;
        }

        const Size max_size = static_cast<Size>(NUM_MAX_LIGHTS_FORWARD_RENDERING) * sizeof(uint32);
        if (!resources.forward_index_buffer)
        {
            RHIBufferDesc default_desc = {};
            default_desc.size = max_size;
            default_desc.usage = RHIResourceUsage::Default;
            default_desc.bind_flags = RHIBindFlags::ShaderResource;
            resources.forward_index_buffer = device->CreateBuffer(default_desc);
            if (!resources.forward_index_buffer)
            {
                return;
            }
            resources.forward_index_buffer->SetName("Forward Light Index Buffer");

            resources.forward_index_srv = {};
            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.buffer_offset = 0;
            srv_desc.buffer_size = max_size;
            srv_desc.buffer_stride = sizeof(uint32);
            if (!device->CreateSubresource(*resources.forward_index_buffer, srv_desc, &resources.forward_index_srv))
            {
                resources.forward_index_buffer = nullptr;
                return;
            }

            RHIBufferDesc upload_desc = {};
            upload_desc.size = max_size;
            upload_desc.usage = RHIResourceUsage::Upload;
            upload_desc.bind_flags = RHIBindFlags::None;
            resources.forward_index_upload_buffer = device->CreateBuffer(upload_desc);
            if (!resources.forward_index_upload_buffer)
            {
                resources.forward_index_buffer = nullptr;
                return;
            }
            resources.forward_index_upload_buffer->SetName("Forward Light Index Upload Buffer");
        }

        const Size upload_size = visible.size() * sizeof(uint32);
        void* mapped = resources.forward_index_upload_buffer->GetMappedData();
        if (!mapped)
        {
            return;
        }
        std::memcpy(mapped, visible.data(), upload_size);

        command_list.TransitionResource(*resources.forward_index_buffer, RHIResourceState::CopyDest);
        command_list.CopyBuffer(*resources.forward_index_buffer, 0, *resources.forward_index_upload_buffer, 0, upload_size);
        command_list.TransitionResource(*resources.forward_index_buffer, RHIResourceState::ShaderRead);

        resources.forward_light_count = static_cast<uint32>(visible.size());
    }

    void RendererInternal::RenderForwardPath(View& view)
    {
        if (!view.scene || view.camera_entity == ecs::INVALID_ENTITY || !current_window)
            return;

        FrameContext& frame_context = GetFrameContext();

        //rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();

        RHICommandList* command_list = frame_context.BeginCommandList(*device);
        if (!command_list)
        {
            return;
        }

        jobsystem::Execute(GetRenderingWorkContext(), [this, pview = &view, command_list](jobsystem::JobArgs args) {
            View& view = *pview;
            FrameContext& frame_context = GetFrameContext();
            rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();


            RHISubresourceBinding back_buffer_binding = {};
            if (!GetCurrentBackBufferBinding(back_buffer_binding))
            {
                return;
            }

            View::RenderTargets& targets = view.render_targets;
            View::ExposureResources& exposure = view.exposure_resources;
            if (!targets.depth || !targets.color[0] || !targets.color[1])
            {
                return;
            }

            RHISubresourceBinding depth_buffer_binding = {};
            depth_buffer_binding.resource = targets.depth.get();
            depth_buffer_binding.subresource = targets.depth_dsv;

            // The scene always renders into the view's color[0]; the post chain ping-pongs between the
            // view targets and the result is composited into the backbuffer at the view's viewport rect.
            RHISubresourceBinding scene_color_binding = {};
            scene_color_binding.resource = targets.color[0].get();
            scene_color_binding.subresource = targets.color_rtv[0];

            Vector<RHISubresourceBinding> color_targets = { scene_color_binding };
            RHISubresourceBinding shader_frame_binding = {};
            shader_frame_binding.resource = shader_frame_buffer.get();
            shader_frame_binding.subresource = shader_frame_buffer_cbv;
            RHISubresourceBinding shader_view_binding = {};
            shader_view_binding.resource = view.view_constants.buffer.get();
            shader_view_binding.subresource = view.view_constants.cbv;

            RHIViewport viewport = {};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(targets.width);
            viewport.height = static_cast<float>(targets.height);
            viewport.min_depth = 0.0f;
            viewport.max_depth = 1.0f;

            RHIRect scissor = {};
            scissor.x = 0;
            scissor.y = 0;
            scissor.width = targets.width;
            scissor.height = targets.height;

            {
                auto cpu_range = profiler::ScopedRangeCPU("Update DDGI Probes");
                UpdateDDGIProbe(frame_context, view, *command_list);
            }

            command_list->TransitionResource(*scene_color_binding.resource, RHIResourceState::RenderTarget);
            command_list->TransitionResource(*depth_buffer_binding.resource, RHIResourceState::DepthWrite);

            command_list->ClearRenderTarget(scene_color_binding, clear_color);
            command_list->ClearDepthStencil(depth_buffer_binding, 0.0f, 0u);

            command_list->SetViewport(viewport);
            command_list->SetScissor(scissor);

            if (gpu_scene.shader_environment.sky_type != SHADER_SKY_TYPE_NONE)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sky Pass", *command_list);
                command_list->BeginEvent("Sky Pass");
                command_list->SetRenderTargets(color_targets, nullptr);
                GraphicsPipelineHash sky_pipeline_hash = {};
                sky_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::SkyPass);
                sky_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                sky_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
                sky_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                sky_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                RHIPipeline* sky_pipeline = shader_library.GetPipeline(sky_pipeline_hash);
                if (!sky_pipeline)
                {
                    command_list->EndEvent();
                    return;
                }
                command_list->SetGraphicsPipeline(*sky_pipeline);
                command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                command_list->Draw(3, 1, 0, 0);
                command_list->EndEvent();
            }

            if (view.shadow_resources.atlas && view.shadow_resources.atlas_dsv.IsValid() && !view.shadow_resources.render_shadow_slices.empty())
            {
                auto cpu_range = profiler::ScopedRangeCPU("Shadow Pass");
                auto gpu_range = profiler::ScopedRangeGPU("Shadow Pass", *command_list);
                command_list->BeginEvent("Fill Shadow Map Atlas");

                RHISubresourceBinding shadow_map_atlas_binding = {};
                shadow_map_atlas_binding.resource = view.shadow_resources.atlas.get();
                shadow_map_atlas_binding.subresource = view.shadow_resources.atlas_dsv;

                command_list->TransitionResource(*view.shadow_resources.atlas, RHIResourceState::DepthWrite);
                command_list->ClearDepthStencil(shadow_map_atlas_binding, 0.0f, 0u);
                command_list->SetRenderTargets({}, &shadow_map_atlas_binding);

                for (uint32 slice_index = 0; slice_index < static_cast<uint32>(view.shadow_resources.render_shadow_slices.size()); ++slice_index)
                {
                    const View::RenderShadowSlice& shadow_slice = view.shadow_resources.render_shadow_slices[slice_index];
                    if (!shadow_slice.HasShadowMapAtlasRect())
                    {
                        continue;
                    }

                    ShaderCamera shadow_camera = {};
                    shadow_camera.Init();

                    shadow_camera.view_projection = shadow_slice.view_projection;

                    if (!UpdateDefaultBuffer(frame_context, *view.view_constants.buffer, &shadow_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer, 0, *command_list))
                    {
                        return;
                    }

                    RHIViewport shadow_viewport = {};
                    shadow_viewport.x = static_cast<float>(shadow_slice.shadow_map_atlas_rect.x);
                    shadow_viewport.y = static_cast<float>(shadow_slice.shadow_map_atlas_rect.y);
                    shadow_viewport.width = static_cast<float>(shadow_slice.shadow_map_atlas_rect.z);
                    shadow_viewport.height = static_cast<float>(shadow_slice.shadow_map_atlas_rect.w);
                    shadow_viewport.min_depth = 0.0f;
                    shadow_viewport.max_depth = 1.0f;
                    command_list->SetViewport(shadow_viewport);

                    RHIRect shadow_scissor = {};
                    shadow_scissor.x = shadow_slice.shadow_map_atlas_rect.x;
                    shadow_scissor.y = shadow_slice.shadow_map_atlas_rect.y;
                    shadow_scissor.width = shadow_slice.shadow_map_atlas_rect.z;
                    shadow_scissor.height = shadow_slice.shadow_map_atlas_rect.w;
                    command_list->SetScissor(shadow_scissor);

                    DrawScene(frame_context, view, RenderPassType::ShadowPass, DrawScene_Opaque, *command_list, slice_index);
                }
                command_list->TransitionResource(*view.shadow_resources.atlas, RHIResourceState::ShaderRead);
                command_list->EndEvent();

                command_list->BeginEvent("Restore Render State");
                ShaderCamera shader_camera{};
                shader_camera.Init();
                shader_camera.view = IDENTITY_MATRIX;
                shader_camera.projection = IDENTITY_MATRIX;
                shader_camera.view_projection = IDENTITY_MATRIX;
                shader_camera.inv_view_projection = IDENTITY_MATRIX;
                if (view.camera_entity != ecs::INVALID_ENTITY)
                {
                    const ecs::CameraComponent* camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
                    if (camera_component)
                    {
                        shader_camera.position = camera_component->eye;
                        shader_camera.forward = camera_component->forward;
                        shader_camera.up = camera_component->up;
                        shader_camera.z_near = camera_component->near_plane;
                        shader_camera.z_far = camera_component->far_plane;
                        shader_camera.internal_resolution = { static_cast<uint32>(view.viewport.width), static_cast<uint32>(view.viewport.height) };
                        shader_camera.internal_resolution_rcp = {
                            view.viewport.width > 0 ? 1.0f / static_cast<float>(view.viewport.width) : 0.0f,
                            view.viewport.height > 0 ? 1.0f / static_cast<float>(view.viewport.height) : 0.0f
                        };
                        shader_camera.viewport_offset = { static_cast<uint32>(view.viewport.x), static_cast<uint32>(view.viewport.y) };
                        shader_camera.view = camera_component->view;
                        shader_camera.projection = camera_component->projection;
                        shader_camera.view_projection = camera_component->view_projection;
                        shader_camera.inv_view_projection = camera_component->inv_view_projection;
                        shader_camera.exposure = camera_component->exposure_multiplier * std::exp2(camera_component->exposure_compensation);
                    }
                }

                if (!UpdateDefaultBuffer(frame_context, *view.view_constants.buffer, &shader_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer, 0, *command_list))
                {
                    return;
                }

                command_list->EndEvent();
            }

            command_list->SetViewport(viewport);
            command_list->SetScissor(scissor);

            // prepass
            {
                auto gpu_range = profiler::ScopedRangeGPU("Prepass", *command_list);
                command_list->BeginEvent("Prepass");
                command_list->SetRenderTargets({}, & depth_buffer_binding);
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Prepass");
                    DrawScene(frame_context, view, RenderPassType::DepthPrepass, DrawScene_Opaque, *command_list);
                }
                command_list->EndEvent();
            }
            
			// light culling for ForwardPlus
            if (view.render_path_type == RenderPathType::ForwardPlus && view.light_resources.cluster_light_count_buffer && view.light_resources.cluster_light_offset_buffer && view.light_resources.cluster_light_index_buffer)
            {
                RHIPipeline* light_cull_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSLightCull));
                if (light_cull_pipeline)
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Cull Lights", *command_list);
                    command_list->BeginEvent("Cull Lights");
                    command_list->TransitionResource(*view.light_resources.cluster_light_count_buffer, RHIResourceState::ShaderWrite);
                    command_list->TransitionResource(*view.light_resources.cluster_light_offset_buffer, RHIResourceState::ShaderWrite);
                    command_list->TransitionResource(*view.light_resources.cluster_light_index_buffer, RHIResourceState::ShaderWrite);
                    command_list->SetComputePipeline(*light_cull_pipeline);

                    RHISubresourceBinding light_cull_frame_binding = {};
                    light_cull_frame_binding.resource = shader_frame_buffer.get();
                    light_cull_frame_binding.subresource = shader_frame_buffer_cbv;
                    RHISubresourceBinding light_cull_camera_binding = {};
                    light_cull_camera_binding.resource = view.view_constants.buffer.get();
                    light_cull_camera_binding.subresource = view.view_constants.cbv;
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 0, light_cull_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 1, light_cull_camera_binding);

                    LightCullPushConstants light_cull_push = {};
                    light_cull_push.Init();
                    light_cull_push.cluster_light_count_uav = static_cast<uint32>(view.light_resources.cluster_light_count_uav.descriptor_index);
                    light_cull_push.cluster_light_offset_uav = static_cast<uint32>(view.light_resources.cluster_light_offset_uav.descriptor_index);
                    light_cull_push.cluster_light_index_uav = static_cast<uint32>(view.light_resources.cluster_light_index_uav.descriptor_index);
                    light_cull_push.cluster_count = view.light_resources.cluster_dims;
                    light_cull_push.light_count = static_cast<uint32>(gpu_scene.shader_lights.size()) - (gpu_scene.has_derived_sun ? 1u : 0u);
                    light_cull_push.depth_slice_count = view.light_resources.depth_slice_count;
                    command_list->PushConstants(RHIShaderStage::Compute, &light_cull_push, sizeof(light_cull_push), 0);

                    command_list->Dispatch(view.light_resources.cluster_dims.x, view.light_resources.cluster_dims.y, 1u);

                    command_list->UAVBarrier(*view.light_resources.cluster_light_count_buffer);
                    command_list->UAVBarrier(*view.light_resources.cluster_light_offset_buffer);
                    command_list->UAVBarrier(*view.light_resources.cluster_light_index_buffer);
                    command_list->TransitionResource(*view.light_resources.cluster_light_count_buffer, RHIResourceState::ShaderRead);
                    command_list->TransitionResource(*view.light_resources.cluster_light_offset_buffer, RHIResourceState::ShaderRead);
                    command_list->TransitionResource(*view.light_resources.cluster_light_index_buffer, RHIResourceState::ShaderRead);
                    command_list->EndEvent();
                }
            }

            // main pass
            {
                auto gpu_range = profiler::ScopedRangeGPU("Main Pass", *command_list);
                command_list->BeginEvent("Main Pass");

                command_list->SetRenderTargets(color_targets, &depth_buffer_binding);
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Main Pass");
                    uint32 main_pass_flags = 0;
                    if ((view.show_flags & Show_Opaque) != 0)
                    {
                        main_pass_flags |= DrawScene_Opaque;
                    }
                    if ((view.show_flags & Show_Transparent) != 0)
                    {
                        main_pass_flags |= DrawScene_Transparent;
                    }
                    DrawScene(frame_context, view, RenderPassType::MainPass, main_pass_flags, *command_list);
                }
                command_list->EndEvent();
            }

            // decal pass: project decal volumes onto the scene depth, blending into the HDR color target.
            if ((view.show_flags & Show_Decals) != 0 && !gpu_scene.shader_decals.empty() && gpu_scene.decal_buffer.srv.IsValid() && view.render_targets.depth_srv.IsValid())
            {
                auto gpu_range = profiler::ScopedRangeGPU("Decal Pass", *command_list);
                command_list->BeginEvent("Decal Pass");

                command_list->TransitionResource(*depth_buffer_binding.resource, RHIResourceState::ShaderRead);
                command_list->SetRenderTargets(color_targets, nullptr);
                command_list->SetViewport(viewport);
                command_list->SetScissor(scissor);
                DrawScene(frame_context, view, RenderPassType::DecalPass, DrawScene_Decal, *command_list);
                command_list->TransitionResource(*depth_buffer_binding.resource, RHIResourceState::DepthWrite);

                command_list->EndEvent();
            }


            // Ping-pong index of the view color buffer holding the current image.
            uint32 src = 0;

            GraphicsPipelineHash composite_pipeline_hash = {};
            composite_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::CompositePass);
            composite_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            composite_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            composite_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            RHIPipeline* composite_pipeline = shader_library.GetPipeline(composite_pipeline_hash);

            // Post chain + resolve
            {
                auto gpu_range = profiler::ScopedRangeGPU("Resolve Post Process", *command_list);
                command_list->BeginEvent("Resolve Post Process");

                const RHITextureDesc& color_desc = targets.color[0]->GetDesc().texture_desc;
                const uint32 width = color_desc.width;
                const uint32 height = color_desc.height;

                const ecs::CameraComponent* camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
                const bool auto_exposure_active = camera_component && camera_component->IsAutoExposure();

                RHIPipeline* tonemap_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSTonemap));

                if (auto_exposure_active && !exposure.luminance_partial_buffer)
                {
                    RHIBufferDesc luminance_partial_desc = {};
                    luminance_partial_desc.size = sizeof(float) * luminance_reduce_group_count;
                    luminance_partial_desc.usage = RHIResourceUsage::Default;
                    luminance_partial_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                    exposure.luminance_partial_buffer = device->CreateBuffer(luminance_partial_desc);
                    if (exposure.luminance_partial_buffer)
                    {
                        exposure.luminance_partial_buffer->SetName("Auto-Exposure Luminance Partials");
                        RHISubresourceDesc luminance_partial_uav_desc = {};
                        luminance_partial_uav_desc.type = RHISubresourceType::UnorderedAccess;
                        luminance_partial_uav_desc.buffer_offset = 0;
                        luminance_partial_uav_desc.buffer_size = sizeof(float) * luminance_reduce_group_count;
                        luminance_partial_uav_desc.buffer_stride = sizeof(float);
                        device->CreateSubresource(*exposure.luminance_partial_buffer, luminance_partial_uav_desc, &exposure.luminance_partial_buffer_uav);

                        RHISubresourceDesc luminance_partial_srv_desc = {};
                        luminance_partial_srv_desc.type = RHISubresourceType::ShaderResource;
                        luminance_partial_srv_desc.buffer_offset = 0;
                        luminance_partial_srv_desc.buffer_size = sizeof(float) * luminance_reduce_group_count;
                        luminance_partial_srv_desc.buffer_stride = sizeof(float);
                        device->CreateSubresource(*exposure.luminance_partial_buffer, luminance_partial_srv_desc, &exposure.luminance_partial_buffer_srv);
                    }
                }
                if (auto_exposure_active && !exposure.luminance_buffer)
                {
                    RHIBufferDesc luminance_buffer_desc = {};
                    luminance_buffer_desc.size = sizeof(float);
                    luminance_buffer_desc.usage = RHIResourceUsage::Default;
                    luminance_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                    exposure.luminance_buffer = device->CreateBuffer(luminance_buffer_desc);
                    if (exposure.luminance_buffer)
                    {
                        exposure.luminance_buffer->SetName("Auto-Exposure Luminance Buffer");
                        RHISubresourceDesc luminance_uav_desc = {};
                        luminance_uav_desc.type = RHISubresourceType::UnorderedAccess;
                        luminance_uav_desc.buffer_offset = 0;
                        luminance_uav_desc.buffer_size = sizeof(float);
                        luminance_uav_desc.buffer_stride = sizeof(float);
                        device->CreateSubresource(*exposure.luminance_buffer, luminance_uav_desc, &exposure.luminance_buffer_uav);
                    }
                }
                if (auto_exposure_active && !exposure.luminance_readback_buffer)
                {
                    RHIBufferDesc luminance_readback_desc = {};
                    luminance_readback_desc.size = sizeof(float);
                    luminance_readback_desc.usage = RHIResourceUsage::Readback;
                    exposure.luminance_readback_buffer = device->CreateBuffer(luminance_readback_desc);
                    if (exposure.luminance_readback_buffer)
                    {
                        exposure.luminance_readback_buffer->SetName("Auto-Exposure Luminance Readback");
                    }
                }
                RHIPipeline* luminance_reduce_pipeline = auto_exposure_active ? shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSLuminanceReduce)) : nullptr;
                RHIPipeline* luminance_resolve_pipeline = auto_exposure_active ? shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSLuminanceResolve)) : nullptr;

                const bool use_fxaa = view.options.aa_mode == AntiAliasingMode::FXAA;
                RHIPipeline* fxaa_pipeline = use_fxaa ? shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSFXAA)) : nullptr;

                if (auto_exposure_active && luminance_reduce_pipeline && luminance_resolve_pipeline && exposure.luminance_partial_buffer && exposure.luminance_buffer && exposure.luminance_readback_buffer)
                {
                    command_list->BeginEvent("Reduce Luminance");
                    command_list->TransitionResource(*targets.color[src], RHIResourceState::ShaderRead);
                    command_list->TransitionResource(*exposure.luminance_partial_buffer, RHIResourceState::ShaderWrite);
                    command_list->SetComputePipeline(*luminance_reduce_pipeline);
                    LuminanceReducePushConstants reduce_push = {};
                    reduce_push.Init();
                    reduce_push.input_descriptor = static_cast<uint32>(targets.color_srv[src].descriptor_index);
                    reduce_push.output_descriptor = static_cast<uint32>(exposure.luminance_partial_buffer_uav.descriptor_index);
                    reduce_push.viewport_size = uint2(static_cast<uint32>(view.viewport.width), static_cast<uint32>(view.viewport.height));
                    reduce_push.viewport_offset = uint2(static_cast<uint32>(view.viewport.x), static_cast<uint32>(view.viewport.y));
                    command_list->PushConstants(RHIShaderStage::Compute, &reduce_push, sizeof(reduce_push), 0);
					command_list->Dispatch(luminance_reduce_group_count, 1u, 1u); // reduce to luminance_reduce_group_count groups of 1D data
                    command_list->UAVBarrier(*exposure.luminance_partial_buffer);
                    command_list->TransitionResource(*exposure.luminance_partial_buffer, RHIResourceState::ShaderRead);
                    command_list->EndEvent();

                    command_list->BeginEvent("Resolve Luminance");
                    command_list->TransitionResource(*exposure.luminance_buffer, RHIResourceState::ShaderWrite);
                    command_list->SetComputePipeline(*luminance_resolve_pipeline);
                    LuminanceReducePushConstants resolve_push = {};
                    resolve_push.Init();
                    resolve_push.input_descriptor = static_cast<uint32>(exposure.luminance_partial_buffer_srv.descriptor_index);
                    resolve_push.output_descriptor = static_cast<uint32>(exposure.luminance_buffer_uav.descriptor_index);
                    command_list->PushConstants(RHIShaderStage::Compute, &resolve_push, sizeof(resolve_push), 0);
                    command_list->Dispatch(1u, 1u, 1u);
                    command_list->UAVBarrier(*exposure.luminance_buffer);
                    command_list->TransitionResource(*exposure.luminance_buffer, RHIResourceState::CopySource);
                    command_list->CopyBuffer(*exposure.luminance_readback_buffer, 0, *exposure.luminance_buffer, 0, sizeof(float));
                    command_list->EndEvent();

                    ecs::CameraComponent* camera = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
                    if (camera)
                    {
                        const float* mapped_luminance = static_cast<const float*>(exposure.luminance_readback_buffer->GetMappedData());
                        if (mapped_luminance)
                        {
                            const float measured = (std::max)(1e-4f, mapped_luminance[0]);

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
                    }
                }

                if (tonemap_pipeline)
                {
                    const uint32 dst = src ^ 1u;
                    command_list->BeginEvent("Tonemap");
                    command_list->TransitionResource(*targets.color[src], RHIResourceState::ShaderRead);
                    command_list->TransitionResource(*targets.color[dst], RHIResourceState::ShaderWrite);

                    command_list->SetComputePipeline(*tonemap_pipeline);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 1, shader_view_binding);
                    TonemapPushConstants tonemap_push = {};
                    tonemap_push.Init();
                    tonemap_push.input_descriptor = static_cast<uint32>(targets.color_srv[src].descriptor_index);
                    tonemap_push.output_descriptor = static_cast<uint32>(targets.color_uav[dst].descriptor_index);
                    tonemap_push.resolution = uint2(width, height);
                    tonemap_push.tonemap_type = view.options.tonemap_mode == TonemapMode::ACES ? TONEMAP_TYPE_ACES : TONEMAP_TYPE_REINHARD;
                    if (view.view_mode != ViewMode::Lit && view.view_mode != ViewMode::Wireframe)
                    {
                        tonemap_push.tonemap_type = TONEMAP_TYPE_NONE;
                    }
                    command_list->PushConstants(RHIShaderStage::Compute, &tonemap_push, sizeof(tonemap_push), 0);
                    command_list->Dispatch((width + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D,
                                           (height + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D, 1u);
                    command_list->UAVBarrier(*targets.color[dst]);
                    command_list->EndEvent();
                    src = dst;
                }

                if (use_fxaa && fxaa_pipeline)
                {
                    const uint32 dst = src ^ 1u;
                    command_list->BeginEvent("FXAA");
                    command_list->TransitionResource(*targets.color[src], RHIResourceState::ShaderRead);
                    command_list->TransitionResource(*targets.color[dst], RHIResourceState::ShaderWrite);

                    command_list->SetComputePipeline(*fxaa_pipeline);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 1, shader_view_binding);
                    FXAAPushConstants fxaa_push = {};
                    fxaa_push.Init();
                    fxaa_push.input_descriptor = static_cast<uint32>(targets.color_srv[src].descriptor_index);
                    fxaa_push.output_descriptor = static_cast<uint32>(targets.color_uav[dst].descriptor_index);
                    fxaa_push.rcp_resolution = float2(1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
                    fxaa_push.resolution = uint2(width, height);
                    command_list->PushConstants(RHIShaderStage::Compute, &fxaa_push, sizeof(fxaa_push), 0);
                    command_list->Dispatch((width + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D,
                                           (height + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D, 1u);
                    command_list->UAVBarrier(*targets.color[dst]);
                    command_list->EndEvent();
                    src = dst;
                }

                command_list->EndEvent();
            }

            command_list->TransitionResource(*targets.color[src], RHIResourceState::RenderTarget);

            RHISubresourceBinding view_output_binding = {};
            view_output_binding.resource = targets.color[src].get();
            view_output_binding.subresource = targets.color_rtv[src];

            if ((view.show_flags & Show_Grid) != 0)
            {
                GraphicsPipelineHash grid_pipeline_hash = {};
                grid_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::GridPass);
                grid_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                grid_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
                grid_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                grid_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                grid_pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(resource::MaterialBlendMode::Transparent);
                if (RHIPipeline* grid_pipeline = shader_library.GetPipeline(grid_pipeline_hash))
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Grid Pass", *command_list);
                    command_list->BeginEvent("Grid Pass");

                    command_list->SetViewport(viewport);
                    command_list->SetScissor(scissor);
                    command_list->SetRenderTargets({ view_output_binding }, &depth_buffer_binding);
                    command_list->SetGraphicsPipeline(*grid_pipeline);
                    command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    command_list->Draw(3, 1, 0, 0);

                    command_list->EndEvent();
                }
            }

            // primitive (line/point) pass: depth-tested against the scene
            {
                auto gpu_range = profiler::ScopedRangeGPU("Primitive Pass", *command_list);
                command_list->BeginEvent("Primitive Pass");

                command_list->SetViewport(viewport);
                command_list->SetScissor(scissor);
                command_list->SetRenderTargets({ view_output_binding }, &depth_buffer_binding);
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Primitive Pass");
                    DrawScene(frame_context, view, RenderPassType::PrimitivePass, DrawScene_Primitive, *command_list);
                }
                command_list->EndEvent();
            }

            // sprite/text 3d pass
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sprite/Text3D Pass", *command_list);
                command_list->BeginEvent("Sprite/Text3D Pass");

                command_list->SetRenderTargets({ view_output_binding }, &depth_buffer_binding);
                if ((view.show_flags & Show_Sprites3D) != 0)
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Sprite/Text3D Pass");
                    DrawScene(frame_context, view, RenderPassType::Sprite3DPass, DrawScene_3DSprite, *command_list);
                }
                command_list->EndEvent();
            }

#ifndef WON_SHIPPING
            // drawn after tonemap so debug colors are authored and displayed in LDR
            {
                auto gpu_range = profiler::ScopedRangeGPU("DebugDraw3D Pass", *command_list);
                command_list->BeginEvent("DebugDraw3D Pass");

                command_list->SetRenderTargets({ view_output_binding }, &depth_buffer_binding);
                BuildDebug3D(view);
                DrawDebug3D(view, *command_list);

                command_list->EndEvent();
            }
#endif

            // sprite 2d pass
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sprite2D Pass", *command_list);
                command_list->BeginEvent("Sprite2D Pass");

                command_list->SetRenderTargets({ view_output_binding }, nullptr);
                if ((view.show_flags & Show_Sprites2D) != 0)
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Sprite2D Pass");
                    DrawScene(frame_context, view, RenderPassType::Sprite2DPass, DrawScene_2DSprite, *command_list);
                }
                command_list->EndEvent();
            }

            // composite the finished view into the backbuffer at its viewport rect
            if (composite_pipeline)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Composite", *command_list);
                command_list->BeginEvent("Composite");

                command_list->TransitionResource(*targets.color[src], RHIResourceState::ShaderRead);
                command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::RenderTarget);
                command_list->SetRenderTargets({ back_buffer_binding }, nullptr);

                RHIViewport composite_viewport = {};
                composite_viewport.x = static_cast<float>(view.viewport.x);
                composite_viewport.y = static_cast<float>(view.viewport.y);
                composite_viewport.width = static_cast<float>(view.viewport.width);
                composite_viewport.height = static_cast<float>(view.viewport.height);
                composite_viewport.min_depth = 0.0f;
                composite_viewport.max_depth = 1.0f;

                RHIRect composite_scissor = {};
                composite_scissor.x = view.scissor.x;
                composite_scissor.y = view.scissor.y;
                composite_scissor.width = view.scissor.width;
                composite_scissor.height = view.scissor.height;

                command_list->SetViewport(composite_viewport);
                command_list->SetScissor(composite_scissor);
                command_list->SetGraphicsPipeline(*composite_pipeline);

                CompositePushConstants composite_push = {};
                composite_push.Init();
                composite_push.input_descriptor = static_cast<uint32>(targets.color_srv[src].descriptor_index);
                command_list->PushConstants(RHIShaderStage::Pixel, &composite_push, sizeof(composite_push), 0);

                command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                command_list->Draw(3, 1, 0, 0);
                command_list->EndEvent();
            }
        });
    }

    void RendererInternal::EndFrame()
    {
        FrameContext& frame_context = GetFrameContext();

        RHISwapchain* swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            return;
        }

        RHISubresourceBinding back_buffer_binding = {};
        if (!GetCurrentBackBufferBinding(back_buffer_binding))
        {
            return;
        }

        {
            auto cpu_range = profiler::ScopedRangeCPU("Wait Render Job");
            jobsystem::Wait(GetRenderingWorkContext());
        }
        RHICommandList* final_command_list = frame_context.BeginCommandList(*device);

        profiler::EndFrameGPU(*final_command_list);
        final_command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::Present);

        RHIContext* graphics_context = device->GetContext(RHIQueueType::Graphics);
        {
            auto cpu_range = profiler::ScopedRangeCPU("Submit Command Lists");
            frame_context.SubmitCommandLists(*graphics_context);
        }

        if (vsync_requested != vsync_enabled)
        {
            vsync_enabled = vsync_requested;
            swapchain->SetVSync(vsync_enabled);
        }

        {
            auto cpu_range = profiler::ScopedRangeCPU("Present");
            if (!swapchain->Present())
            {
                backlog::Post("failed to present swapchain", backlog::LogLevel::Error);
                return;
            }
        }

        ++frame_count;
        current_frame_slot = (current_frame_slot + 1) % static_cast<uint32>(frame_contexts.size());

        if (enqueued_work_fence_value == 0 && enqueued_work_recorded)
        {
            enqueued_work_fence_value = graphics_context->Submit(*enqueued_work_command_list, enqueued_work_fence.get());
            if (!enqueued_work_succeeded)
            {
                backlog::Post("failed to flush enqueued rendering work", backlog::LogLevel::Warning);
            }
        }
    }

    void RendererInternal::WaitIdle()
    {
        jobsystem::Wait(GetRenderingWorkContext());
        for (Size i = 0; i < static_cast<Size>(RHIQueueType::Count); ++i)
        {
            auto context = device->GetContext(static_cast<RHIQueueType>(i));
            context->WaitIdle();
        }
        enqueued_work_fence_value = 0;
        enqueued_work_scratch_resources.clear();
    }

    void RendererInternal::Shutdown()
    {
        WaitIdle();

        current_window = nullptr;
        for (FrameContext& frame_context : frame_contexts)
        {
            for (Vector<FrameCommandList>& command_lists : frame_context.command_lists)
            {
                command_lists.clear();
            }
            for (std::atomic<Size>& command_list_count : frame_context.command_list_counts)
            {
                command_list_count.store(0, std::memory_order_relaxed);
            }
            frame_context.fence = nullptr;
            {
                std::scoped_lock lock(frame_context.frame_upload_mutex);
                frame_context.frame_upload_buffer = nullptr;
                frame_context.frame_upload_offset = 0;
            }
            frame_context.fence_value = 0;
            {
                std::scoped_lock lock(frame_context.deferred_res_removal_mutex);
                frame_context.deferred_res_removal.clear();
            }
        }
        current_frame_slot = 0;
        shader_frame_buffer_cbv = {};
        shader_frame_buffer = nullptr;
#ifndef WON_SHIPPING
        debug_3d_buffer = nullptr;
        debug_3d_buffer_srv = {};
#endif
        back_buffers_rtv = {};
        shader_library.ClearAll();
        device = nullptr;
    }
}
