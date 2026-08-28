#include "RendererInternal.h"
#include "ShaderInterop_Sprite.h"
#include "ShaderInterop_Decal.h"
#include "ShaderInterop_Water.h"
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
#include "FrameGraph.h"
#include "ShaderLibrary.h"
#include "MathUtils.h"

#include "Window.h"
#include "Entity.h"
#include "CameraComponent.h"
#include "RectPacker.h"

#include "ShaderInterop.h"
#include "ShaderInterop_PostProcess.h"
#include "ShaderInterop_LightCull.h"
#include "ShaderInterop_Occlusion.h"

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

    bool RendererInternal::UpdateFrameConstants(FrameContext& frame_context, const View& view)
    {
        ShaderFrame shader_frame{};
        shader_frame.Init();
        ShaderView shader_view{};
        shader_view.Init();
        rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();
        shader_frame.frame_slot = current_frame_slot;
        shader_frame.scene.instancebuffer = gpu_scene.instance_buffer.srv.descriptor_index;
        shader_frame.scene.geometrybuffer = gpu_scene.geometry_buffer.srv.descriptor_index;
        shader_frame.scene.materialbuffer = gpu_scene.material_buffer.srv.descriptor_index;
        shader_frame.scene.lightbuffer = gpu_scene.light_buffer.srv.descriptor_index;
        shader_frame.scene.directional_count = gpu_scene.directional_count;
        shader_frame.scene.light_count = static_cast<uint32>(gpu_scene.shader_lights.size()) - (gpu_scene.has_derived_sun ? 1u : 0u);
        if (view.render_path_type == RenderPathType::Forward && view.light_resources.forward_index_buffer != invalid_frame_resource && view.light_resources.forward_light_count > 0)
        {
            shader_view.forward_light_index_buffer = view.light_resources.forward_index_srv.descriptor_index;
            shader_view.forward_light_count = view.light_resources.forward_light_count;
        }
        if (view.render_path_type == RenderPathType::ForwardPlus && view.light_resources.cluster_light_count_buffer != invalid_frame_resource && view.light_resources.cluster_light_offset_buffer != invalid_frame_resource && view.light_resources.cluster_light_index_buffer != invalid_frame_resource)
        {
            shader_view.cluster_light_count_buffer = view.light_resources.cluster_light_count_srv.descriptor_index;
            shader_view.cluster_light_offset_buffer = view.light_resources.cluster_light_offset_srv.descriptor_index;
            shader_view.cluster_light_index_buffer = view.light_resources.cluster_light_index_srv.descriptor_index;
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
        shader_frame.time = static_cast<float>(view.scene->GetSimulation().elapsed_seconds);
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

        const FrameResourceId frame_constants_id = frame_graph.Import(*shader_frame_buffer);
        frame_graph.MarkNoCull(frame_constants_id);
        if (!frame_graph.QueueBufferUpload(frame_constants_id, &shader_frame, sizeof(ShaderFrame)))
        {
            return false;
        }
        if (!frame_graph.QueueBufferUpload(frame_graph.Import(*view.view_constants.buffer), &shader_view, sizeof(ShaderView)))
        {
            return false;
        }

        return true;
    }

    namespace
    {
        bool UploadBuffer(GPUBuffer& target, const char* name, FrameContext& frame_context,
            const void* data, Size size, Size stride,
            RHIDevice& device, FrameGraph& frame_graph)
        {
            if (size == 0)
            {
                if (target.buffer)
                {
                    frame_context.RemoveResourceDeferred(std::move(target.buffer));
                }
                target.buffer = nullptr;
                target.srv = {};
                return true;
            }

            const Size current_buffer_size = target.buffer ? target.buffer->GetDesc().buffer_desc.size : 0;
            if (!target.buffer || current_buffer_size < size)
            {
                if (target.buffer)
                {
                    frame_context.RemoveResourceDeferred(std::move(target.buffer));
                }

                RHIBufferDesc buffer_desc = {};
                buffer_desc.size = size;
                buffer_desc.usage = RHIResourceUsage::Default;
                buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                target.buffer = device.CreateBuffer(buffer_desc);
                if (!target.buffer)
                {
                    backlog::Post("GPUScene upload: failed to create default buffer", backlog::LogLevel::Error);
                    return false;
                }
                target.buffer->SetName(name);

                target.srv = {};
                RHISubresourceDesc srv_desc = {};
                srv_desc.type = RHISubresourceType::ShaderResource;
                srv_desc.buffer_offset = 0;
                srv_desc.buffer_size = target.buffer->GetDesc().buffer_desc.size;
                srv_desc.buffer_stride = stride;
                if (!device.CreateSubresource(*target.buffer, srv_desc, &target.srv))
                {
                    backlog::Post("GPUScene upload: failed to create buffer subresource", backlog::LogLevel::Error);
                    target.buffer = nullptr;
                    return false;
                }
            }

            const FrameResourceId target_id = frame_graph.Import(*target.buffer);
            frame_graph.MarkNoCull(target_id);
            return frame_graph.QueueBufferUpload(target_id, data, size);
        }
    }

    static won::console::ConsoleVariable r_upload_budget("r.upload_budget", 8, "max queued resource uploads per frame, 0 = unlimited", won::console::ConsoleVariableFlagNone);
    static won::console::ConsoleVariable r_cluster_depth_slices("r.cluster.depth_slices", 32, "Forward+ cluster depth slices (1 = 2D tiled)", won::console::ConsoleVariableFlagArchive);
    static won::console::ConsoleVariable r_occlusion_enabled("r.occlusion.enabled", 0, "force hardware occlusion culling on for every view (0 = follow the per-view option)", won::console::ConsoleVariableFlagNone);
    static won::console::ConsoleVariable r_occlusion_bounds_expand("r.occlusion.bounds_expand", 0.005f, "occlusion query bounds expansion as a fraction of the distance to the bounds", won::console::ConsoleVariableFlagNone);

    bool RendererInternal::UploadSceneData(FrameContext& frame_context, ecs::Scene& scene, GPUScene& gpu_scene)
    {
        auto upload_cpu_range = profiler::ScopedRangeCPU("Upload GPU Scene Data");

        {
            GPUScene::WaterResources& water = gpu_scene.water;
            // !! remove any extra zone simulations that are no longer needed
			for (Size i = water.shader_zones.size(); i < water.zone_simulations.size(); ++i) 
            {
                GPUScene::WaterResources::ZoneSimulation& simulation = water.zone_simulations[i];
                frame_context.RemoveResourceDeferred(std::move(simulation.height_texture[0]));
                frame_context.RemoveResourceDeferred(std::move(simulation.height_texture[1]));
                frame_context.RemoveResourceDeferred(std::move(simulation.wetness_texture));
            }
            water.zone_simulations.resize(water.shader_zones.size());

            RHITextureDesc texture_desc = {};
            texture_desc.depth = 1;
            texture_desc.mip_levels = 1;
            texture_desc.array_layers = 1;
            texture_desc.sample_count = 1;
            texture_desc.format = RHIFormat::R32Float;
            texture_desc.usage = RHIResourceUsage::Default;
            texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess | RHIBindFlags::RenderTarget;

            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.format = texture_desc.format;
            RHISubresourceDesc uav_desc = srv_desc;
            uav_desc.type = RHISubresourceType::UnorderedAccess;
            RHISubresourceDesc rtv_desc = srv_desc;
            rtv_desc.type = RHISubresourceType::RenderTarget;

            static const char* height_names[2] = { "Water Zone Height 0", "Water Zone Height 1" };
            const uint32 result_index = static_cast<uint32>(scene.GetWaterSimulation().step_count % 2ull);
            Vector<float> zero_texels;

            for (Size zone_index = 0; zone_index < water.zone_simulations.size(); ++zone_index)
            {
                GPUScene::WaterResources::ZoneSimulation& simulation = water.zone_simulations[zone_index];
                ShaderWaterZone& zone = water.shader_zones[zone_index];
                texture_desc.width = zone.ripple_resolution;
                texture_desc.height = zone.ripple_resolution;

                const Size texel_count = static_cast<Size>(zone.ripple_resolution) * zone.ripple_resolution;
                const Size zero_size = texel_count * sizeof(float);

                for (uint32 i = 0; i < 2; ++i)
                {
                    if (simulation.height_texture[i] && simulation.height_texture[i]->GetDesc().texture_desc.width != zone.ripple_resolution)
                    {
                        frame_context.RemoveResourceDeferred(std::move(simulation.height_texture[i]));
                    }
                    if (simulation.height_texture[i])
                    {
                        continue;
                    }
                    if (zero_texels.size() < texel_count)
                    {
                        zero_texels.assign(texel_count, 0.0f);
                    }
                    simulation.height_texture[i] = device->CreateTexture(texture_desc, zero_texels.data(), zero_size);
                    if (!simulation.height_texture[i])
                    {
                        backlog::Post("failed to create water zone height texture", backlog::LogLevel::Error);
                        return false;
                    }
                    simulation.height_texture[i]->SetName(height_names[i]);

                    if (!device->CreateSubresource(*simulation.height_texture[i], srv_desc, &simulation.height_srv[i])
                        || !device->CreateSubresource(*simulation.height_texture[i], uav_desc, &simulation.height_uav[i])
                        || !device->CreateSubresource(*simulation.height_texture[i], rtv_desc, &simulation.height_rtv[i]))
                    {
                        backlog::Post("failed to create water zone height subresource", backlog::LogLevel::Error);
                        simulation.height_texture[i] = nullptr;
                        return false;
                    }
                }

                if (simulation.wetness_texture && simulation.wetness_texture->GetDesc().texture_desc.width != zone.ripple_resolution)
                {
                    frame_context.RemoveResourceDeferred(std::move(simulation.wetness_texture));
                }
                if (!simulation.wetness_texture)
                {
                    if (zero_texels.size() < texel_count)
                    {
                        zero_texels.assign(texel_count, 0.0f);
                    }
                    simulation.wetness_texture = device->CreateTexture(texture_desc, zero_texels.data(), zero_size);
                    if (!simulation.wetness_texture)
                    {
                        backlog::Post("failed to create water zone wetness texture", backlog::LogLevel::Error);
                        return false;
                    }
                    simulation.wetness_texture->SetName("Water Zone Wetness");

                    if (!device->CreateSubresource(*simulation.wetness_texture, srv_desc, &simulation.wetness_srv)
                        || !device->CreateSubresource(*simulation.wetness_texture, uav_desc, &simulation.wetness_uav))
                    {
                        backlog::Post("failed to create water zone wetness subresource", backlog::LogLevel::Error);
                        simulation.wetness_texture = nullptr;
                        return false;
                    }
                }

                zone.ripple_texture = static_cast<int32>(simulation.height_srv[result_index].descriptor_index);
                zone.wetness_texture = static_cast<int32>(simulation.wetness_srv.descriptor_index);
            }
        }

        // upload the scene buffers
        UploadBuffer(gpu_scene.light_buffer, "Scene Light Buffer", frame_context, gpu_scene.shader_lights.data(), gpu_scene.shader_lights.size() * sizeof(ShaderLight), sizeof(ShaderLight), *device, frame_graph);
        UploadBuffer(gpu_scene.geometry_buffer, "Scene Geometry Buffer", frame_context, gpu_scene.shader_geometries.data(), gpu_scene.shader_geometries.size() * sizeof(ShaderGeometry), sizeof(ShaderGeometry), *device, frame_graph);
        UploadBuffer(gpu_scene.material_buffer, "Scene Material Buffer", frame_context, gpu_scene.shader_materials.data(), gpu_scene.shader_materials.size() * sizeof(ShaderMaterial), sizeof(ShaderMaterial), *device, frame_graph);
        UploadBuffer(gpu_scene.bone_buffer, "Scene Bone Matrix Buffer", frame_context, gpu_scene.shader_bone_matrices.data(), gpu_scene.shader_bone_matrices.size() * sizeof(float4), sizeof(float4), *device, frame_graph);
        UploadBuffer(gpu_scene.instance_buffer, "Scene Instance Buffer", frame_context, gpu_scene.shader_instances.data(), gpu_scene.shader_instances.size() * sizeof(ShaderInstance), sizeof(ShaderInstance), *device, frame_graph);
        UploadBuffer(gpu_scene.particle_buffer, "Scene Particle Buffer", frame_context, gpu_scene.particle_instances.data(), gpu_scene.particle_instances.size() * sizeof(float4), sizeof(float4), *device, frame_graph);
        UploadBuffer(gpu_scene.decal_buffer, "Scene Decal Buffer", frame_context, gpu_scene.shader_decals.data(), gpu_scene.shader_decals.size() * sizeof(ShaderDecal), sizeof(ShaderDecal), *device, frame_graph);
        UploadBuffer(gpu_scene.water.body_buffer, "Scene Water Body Buffer", frame_context, gpu_scene.water.shader_bodies.data(), gpu_scene.water.shader_bodies.size() * sizeof(ShaderWaterBody), sizeof(ShaderWaterBody), *device, frame_graph);
        UploadBuffer(gpu_scene.water.zone_buffer, "Scene Water Zone Buffer", frame_context, gpu_scene.water.shader_zones.data(), gpu_scene.water.shader_zones.size() * sizeof(ShaderWaterZone), sizeof(ShaderWaterZone), *device, frame_graph);
        UploadBuffer(gpu_scene.bvh_node_buffer, "Scene BVH Node Buffer", frame_context, gpu_scene.shader_bvh_nodes.data(), gpu_scene.shader_bvh_nodes.size() * sizeof(ShaderBVHNode), sizeof(ShaderBVHNode), *device, frame_graph);
        UploadBuffer(gpu_scene.bvh_instance_buffer, "Scene BVH Instance Buffer", frame_context, gpu_scene.shader_bvh_instances.data(), gpu_scene.shader_bvh_instances.size() * sizeof(ShaderBVHInstance), sizeof(ShaderBVHInstance), *device, frame_graph);

        Vector<std::shared_ptr<resource::Mesh>> released_meshes;
        rendering::utils::TakeEnqueuedMeshReleases(released_meshes);
        for (const std::shared_ptr<resource::Mesh>& mesh : released_meshes)
        {
            frame_context.RemoveSharedResourceDeferred(mesh->render_data.buffer);
            if (mesh->gpu_bvh.node_buffer)
            {
                frame_context.RemoveSharedResourceDeferred(mesh->gpu_bvh.node_buffer);
            }
            if (mesh->gpu_bvh.primitive_buffer)
            {
                frame_context.RemoveSharedResourceDeferred(mesh->gpu_bvh.primitive_buffer);
            }
        }

        Vector<std::shared_ptr<resource::Mesh>> updated_meshes;
        rendering::utils::TakeEnqueuedVertexStreamUpdates(updated_meshes);

        Vector<std::shared_ptr<resource::Mesh>> normal_meshes;
        Vector<FrameResourceAccess> normal_accesses;
        for (const std::shared_ptr<resource::Mesh>& mesh : updated_meshes)
        {
            const resource::Mesh::RenderData& render_data = mesh->render_data;
            if (!render_data.IsValid())
            {
                continue;
            }

            const Size slot_count = mesh->dynamic_vertex_streams ? static_cast<Size>(max_frames_in_flight) : 1;
            const Size positions_size = mesh->positions.size() * sizeof(float3);
            if (positions_size != render_data.positions.size / slot_count)
            {
                continue;
            }

            const FrameResourceId mesh_buffer_id = frame_graph.Import(*render_data.buffer);
            frame_graph.MarkNoCull(mesh_buffer_id);
            frame_graph.QueueBufferUpload(mesh_buffer_id, mesh->positions.data(), positions_size, render_data.positions.offset + current_frame_slot * positions_size);

            if (mesh->dynamic_vertex_streams && render_data.normals.uav.IsValid() && render_data.adjacency_ranges.IsValid() && render_data.adjacency_triangles.IsValid())
            {
                normal_meshes.push_back(mesh);
                normal_accesses.push_back({ mesh_buffer_id, RHIResourceState::Undefined, FrameResourceAccess::Type::ReadWrite });
            }
        }

        if (!normal_meshes.empty())
        {
            if (RHIPipeline* mesh_normal_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSMeshNormal)))
            {
                const uint32 vertex_slot = current_frame_slot;
                frame_graph.AddPass("Mesh Normal Pass", std::move(normal_accesses),
                    [this, mesh_normal_pipeline, normal_meshes, vertex_slot](const FrameGraphPassContext& pass_context)
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Mesh Normal Pass", (*pass_context.command_list));

                    pass_context.command_list->SetComputePipeline(*mesh_normal_pipeline);
                    for (const std::shared_ptr<resource::Mesh>& mesh : normal_meshes)
                    {
                        const resource::Mesh::RenderData& render_data = mesh->render_data;

                        MeshNormalPushConstants mesh_normal_push = {};
                        mesh_normal_push.Init();
                        mesh_normal_push.position_descriptor = static_cast<uint32>(render_data.positions.srv.descriptor_index);
                        mesh_normal_push.index_descriptor = static_cast<uint32>(render_data.indices.srv.descriptor_index);
                        mesh_normal_push.adjacency_range_descriptor = static_cast<uint32>(render_data.adjacency_ranges.srv.descriptor_index);
                        mesh_normal_push.adjacency_triangle_descriptor = static_cast<uint32>(render_data.adjacency_triangles.srv.descriptor_index);
                        mesh_normal_push.normal_uav_descriptor = static_cast<uint32>(render_data.normals.uav.descriptor_index);
                        mesh_normal_push.vertex_count = static_cast<uint32>(mesh->positions.size());
                        mesh_normal_push.stream_offset = vertex_slot * static_cast<uint32>(mesh->positions.size());

                        pass_context.command_list->PushConstants(RHIShaderStage::Compute, &mesh_normal_push, sizeof(mesh_normal_push), 0);
                        pass_context.command_list->Dispatch((mesh_normal_push.vertex_count + DISPATCH_THREAD_GROUP_1D - 1) / DISPATCH_THREAD_GROUP_1D, 1u, 1u);
                    }
                    for (const std::shared_ptr<resource::Mesh>& mesh : normal_meshes)
                    {
                        pass_context.command_list->UAVBarrier(*mesh->render_data.buffer);
                    }
                });
            }
        }

        const bool use_sky_lighting = gpu_scene.shader_environment.sky_type != SHADER_SKY_TYPE_NONE
            && (gpu_scene.shader_environment.diffuse_gi_mode == SHADER_DIFFUSE_GI_MODE_SKY
                || gpu_scene.shader_environment.reflection_mode == SHADER_REFLECTION_MODE_SKY);
        if (use_sky_lighting)
        {
            // create sky lighting resources
            const bool diffuse_from_sky = gpu_scene.shader_environment.diffuse_gi_mode == SHADER_DIFFUSE_GI_MODE_SKY;
            const bool specular_from_sky = gpu_scene.shader_environment.reflection_mode == SHADER_REFLECTION_MODE_SKY;
            if (!diffuse_from_sky && !specular_from_sky)
            {
                return false;
            }

            if (!gpu_scene.sky_lighting.capture_texture)
            {
                RHITextureDesc desc = {};
                desc.width = sky_capture_resolution;
                desc.height = sky_capture_resolution;
                desc.depth = 1;
                desc.mip_levels = 1;
                desc.array_layers = 6;
                desc.is_cube = true;
                desc.sample_count = 1;
                desc.format = RHIFormat::R16G16B16A16Float;
                desc.usage = RHIResourceUsage::Default;
                desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                gpu_scene.sky_lighting.capture_texture = device->CreateTexture(desc);
                if (!gpu_scene.sky_lighting.capture_texture)
                {
                    return false;
                }
                gpu_scene.sky_lighting.capture_texture->SetName("Sky Capture Cubemap");

                RHISubresourceDesc srv_desc = {};
                srv_desc.type = RHISubresourceType::ShaderResource;
                srv_desc.format = desc.format;
                srv_desc.first_mip = 0;
                srv_desc.mip_count = 1;
                srv_desc.first_slice = 0;
                srv_desc.slice_count = 6;
                device->CreateSubresource(*gpu_scene.sky_lighting.capture_texture, srv_desc, &gpu_scene.sky_lighting.capture_srv);

                RHISubresourceDesc uav_desc = {};
                uav_desc.type = RHISubresourceType::UnorderedAccess;
                uav_desc.format = desc.format;
                uav_desc.first_mip = 0;
                uav_desc.mip_count = 1;
                uav_desc.first_slice = 0;
                uav_desc.slice_count = 6;
                device->CreateSubresource(*gpu_scene.sky_lighting.capture_texture, uav_desc, &gpu_scene.sky_lighting.capture_uav);
            }

            if (diffuse_from_sky && !gpu_scene.sky_lighting.irradiance_texture)
            {
                RHITextureDesc desc = {};
                desc.width = sky_irradiance_resolution;
                desc.height = sky_irradiance_resolution;
                desc.depth = 1;
                desc.mip_levels = 1;
                desc.array_layers = 6;
                desc.is_cube = true;
                desc.sample_count = 1;
                desc.format = RHIFormat::R16G16B16A16Float;
                desc.usage = RHIResourceUsage::Default;
                desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                gpu_scene.sky_lighting.irradiance_texture = device->CreateTexture(desc);
                if (!gpu_scene.sky_lighting.irradiance_texture)
                {
                    return false;
                }
                gpu_scene.sky_lighting.irradiance_texture->SetName("Sky Irradiance Cubemap");

                RHISubresourceDesc srv_desc = {};
                srv_desc.type = RHISubresourceType::ShaderResource;
                srv_desc.format = desc.format;
                srv_desc.first_mip = 0;
                srv_desc.mip_count = 1;
                srv_desc.first_slice = 0;
                srv_desc.slice_count = 6;
                device->CreateSubresource(*gpu_scene.sky_lighting.irradiance_texture, srv_desc, &gpu_scene.sky_lighting.irradiance_srv);

                RHISubresourceDesc uav_desc = {};
                uav_desc.type = RHISubresourceType::UnorderedAccess;
                uav_desc.format = desc.format;
                uav_desc.first_mip = 0;
                uav_desc.mip_count = 1;
                uav_desc.first_slice = 0;
                uav_desc.slice_count = 6;
                device->CreateSubresource(*gpu_scene.sky_lighting.irradiance_texture, uav_desc, &gpu_scene.sky_lighting.irradiance_uav);
            }

            if (specular_from_sky && !gpu_scene.sky_lighting.specular_texture)
            {
                RHITextureDesc desc = {};
                desc.width = sky_specular_resolution;
                desc.height = sky_specular_resolution;
                desc.depth = 1;
                desc.mip_levels = sky_specular_mip_count;
                desc.array_layers = 6;
                desc.is_cube = true;
                desc.sample_count = 1;
                desc.format = RHIFormat::R16G16B16A16Float;
                desc.usage = RHIResourceUsage::Default;
                desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                gpu_scene.sky_lighting.specular_texture = device->CreateTexture(desc);
                if (!gpu_scene.sky_lighting.specular_texture)
                {
                    return false;
                }
                gpu_scene.sky_lighting.specular_texture->SetName("Sky Specular Cubemap");

                RHISubresourceDesc srv_desc = {};
                srv_desc.type = RHISubresourceType::ShaderResource;
                srv_desc.format = desc.format;
                srv_desc.first_mip = 0;
                srv_desc.mip_count = sky_specular_mip_count;
                srv_desc.first_slice = 0;
                srv_desc.slice_count = 6;
                device->CreateSubresource(*gpu_scene.sky_lighting.specular_texture, srv_desc, &gpu_scene.sky_lighting.specular_srv);

                for (uint32 mip = 0; mip < sky_specular_mip_count; ++mip)
                {
                    RHISubresourceDesc uav_desc = {};
                    uav_desc.type = RHISubresourceType::UnorderedAccess;
                    uav_desc.format = desc.format;
                    uav_desc.first_mip = mip;
                    uav_desc.mip_count = 1;
                    uav_desc.first_slice = 0;
                    uav_desc.slice_count = 6;
                    device->CreateSubresource(*gpu_scene.sky_lighting.specular_texture, uav_desc, &gpu_scene.sky_lighting.specular_mip_uav[mip]);
                }
            }
        }
        else if (gpu_scene.sky_lighting.capture_texture)
        {
            // release sky lighting resources
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.sky_lighting.capture_texture));
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.sky_lighting.irradiance_texture));
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.sky_lighting.specular_texture));
            gpu_scene.sky_lighting.capture_srv = {};
            gpu_scene.sky_lighting.capture_uav = {};
            gpu_scene.sky_lighting.irradiance_srv = {};
            gpu_scene.sky_lighting.irradiance_uav = {};
            gpu_scene.sky_lighting.specular_srv = {};
            for (uint32 mip = 0; mip < sky_specular_mip_count; ++mip)
            {
                gpu_scene.sky_lighting.specular_mip_uav[mip] = {};
            }
            gpu_scene.sky_lighting.signature = {};
            gpu_scene.sky_lighting.pending_irradiance_face = -1;
            gpu_scene.sky_lighting.pending_specular_mip = -1;
            gpu_scene.sky_lighting.valid = false;
        }

        const bool use_ddgi = (gpu_scene.shader_ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) != 0;
        if (use_ddgi)
        {
            // create ddgi resources
            const bool recreate_ddgi_texture =
                !gpu_scene.ddgi.irradiance_texture ||
                !gpu_scene.ddgi.irradiance_texture_srv.IsValid() ||
                !gpu_scene.ddgi.irradiance_texture_uav.IsValid() ||
                !gpu_scene.ddgi.irradiance_history_texture ||
                !gpu_scene.ddgi.irradiance_history_texture_srv.IsValid() ||
                !gpu_scene.ddgi.visibility_texture ||
                !gpu_scene.ddgi.visibility_texture_srv.IsValid() ||
                !gpu_scene.ddgi.visibility_texture_uav.IsValid() ||
                !gpu_scene.ddgi.visibility_history_texture ||
                !gpu_scene.ddgi.visibility_history_texture_srv.IsValid() ||
                !gpu_scene.ddgi.probe_data_buffer ||
                !gpu_scene.ddgi.probe_data_buffer_srv.IsValid() ||
                !gpu_scene.ddgi.probe_data_buffer_uav.IsValid() ||
                !gpu_scene.ddgi.probe_data_history_buffer ||
                !gpu_scene.ddgi.probe_data_history_buffer_srv.IsValid() ||
                gpu_scene.ddgi.probe_counts.x != gpu_scene.shader_ddgi_volume.probe_counts.x ||
                gpu_scene.ddgi.probe_counts.y != gpu_scene.shader_ddgi_volume.probe_counts.y ||
                gpu_scene.ddgi.probe_counts.z != gpu_scene.shader_ddgi_volume.probe_counts.z;

            if (!recreate_ddgi_texture)
            {
                const bool reset_ddgi_history =
                    gpu_scene.ddgi.probe_spacing.x != gpu_scene.shader_ddgi_volume.probe_spacing.x ||
                    gpu_scene.ddgi.probe_spacing.y != gpu_scene.shader_ddgi_volume.probe_spacing.y ||
                    gpu_scene.ddgi.probe_spacing.z != gpu_scene.shader_ddgi_volume.probe_spacing.z ||
                    gpu_scene.ddgi.volume_min.x != gpu_scene.shader_ddgi_volume.volume_min.x ||
                    gpu_scene.ddgi.volume_min.y != gpu_scene.shader_ddgi_volume.volume_min.y ||
                    gpu_scene.ddgi.volume_min.z != gpu_scene.shader_ddgi_volume.volume_min.z ||
                    gpu_scene.ddgi.max_distance != gpu_scene.shader_ddgi_volume.max_distance;
                if (reset_ddgi_history)
                {
                    gpu_scene.ddgi.probe_spacing = gpu_scene.shader_ddgi_volume.probe_spacing;
                    gpu_scene.ddgi.volume_min = gpu_scene.shader_ddgi_volume.volume_min;
                    gpu_scene.ddgi.max_distance = gpu_scene.shader_ddgi_volume.max_distance;
                    gpu_scene.ddgi.probe_update_offset = 0;
                    gpu_scene.ddgi.history_valid = false;
                }
            }
            else
            {
                frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.irradiance_texture));
                frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.irradiance_history_texture));
                frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.visibility_texture));
                frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.visibility_history_texture));
                frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.probe_data_buffer));
                frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.probe_data_history_buffer));
                gpu_scene.ddgi.irradiance_texture_srv = {};
                gpu_scene.ddgi.irradiance_texture_uav = {};
                gpu_scene.ddgi.irradiance_history_texture_srv = {};
                gpu_scene.ddgi.visibility_texture_srv = {};
                gpu_scene.ddgi.visibility_texture_uav = {};
                gpu_scene.ddgi.visibility_history_texture_srv = {};
                gpu_scene.ddgi.probe_data_buffer_srv = {};
                gpu_scene.ddgi.probe_data_buffer_uav = {};
                gpu_scene.ddgi.probe_data_history_buffer_srv = {};
                gpu_scene.ddgi.probe_counts = { 0, 0, 0 };
                gpu_scene.ddgi.probe_spacing = { 0.0f, 0.0f, 0.0f };
                gpu_scene.ddgi.volume_min = { 0.0f, 0.0f, 0.0f };
                gpu_scene.ddgi.max_distance = 0.0f;
                gpu_scene.ddgi.probe_update_offset = 0;
                gpu_scene.ddgi.history_valid = false;

                RHITextureDesc ddgi_irradiance_texture_desc = {};
                ddgi_irradiance_texture_desc.width = (std::max)(gpu_scene.shader_ddgi_volume.probe_counts.x, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2);
                ddgi_irradiance_texture_desc.height = (std::max)(gpu_scene.shader_ddgi_volume.probe_counts.y, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2);
                ddgi_irradiance_texture_desc.depth = 1;
                ddgi_irradiance_texture_desc.mip_levels = 1;
                ddgi_irradiance_texture_desc.array_layers = (std::max)(gpu_scene.shader_ddgi_volume.probe_counts.z, 1u);
                ddgi_irradiance_texture_desc.sample_count = 1;
                ddgi_irradiance_texture_desc.format = RHIFormat::R16G16B16A16Float;
                ddgi_irradiance_texture_desc.usage = RHIResourceUsage::Default;
                ddgi_irradiance_texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                gpu_scene.ddgi.irradiance_texture = device->CreateTexture(ddgi_irradiance_texture_desc);
                if (!gpu_scene.ddgi.irradiance_texture)
                {
                    backlog::Post("failed to create ddgi irradiance texture", backlog::LogLevel::Error);
                    return false;
                }
                gpu_scene.ddgi.irradiance_texture->SetName("DDGI Irradiance Texture");

                RHISubresourceDesc ddgi_irradiance_srv_desc = {};
                ddgi_irradiance_srv_desc.type = RHISubresourceType::ShaderResource;
                ddgi_irradiance_srv_desc.format = ddgi_irradiance_texture_desc.format;
                ddgi_irradiance_srv_desc.first_slice = 0;
                ddgi_irradiance_srv_desc.slice_count = ddgi_irradiance_texture_desc.array_layers;
                ddgi_irradiance_srv_desc.first_mip = 0;
                ddgi_irradiance_srv_desc.mip_count = 1;
                if (!device->CreateSubresource(*gpu_scene.ddgi.irradiance_texture, ddgi_irradiance_srv_desc, &gpu_scene.ddgi.irradiance_texture_srv))
                {
                    backlog::Post("failed to create ddgi irradiance srv", backlog::LogLevel::Error);
                    gpu_scene.ddgi.irradiance_texture = nullptr;
                    return false;
                }

                RHISubresourceDesc ddgi_irradiance_uav_desc = {};
                ddgi_irradiance_uav_desc.type = RHISubresourceType::UnorderedAccess;
                ddgi_irradiance_uav_desc.format = ddgi_irradiance_texture_desc.format;
                ddgi_irradiance_uav_desc.first_mip = 0;
                ddgi_irradiance_uav_desc.mip_count = 1;
                ddgi_irradiance_uav_desc.first_slice = 0;
                ddgi_irradiance_uav_desc.slice_count = ddgi_irradiance_texture_desc.array_layers;
                if (!device->CreateSubresource(*gpu_scene.ddgi.irradiance_texture, ddgi_irradiance_uav_desc, &gpu_scene.ddgi.irradiance_texture_uav))
                {
                    backlog::Post("failed to create ddgi irradiance uav", backlog::LogLevel::Error);
                    gpu_scene.ddgi.irradiance_texture = nullptr;
                    return false;
                }

                gpu_scene.ddgi.irradiance_history_texture = device->CreateTexture(ddgi_irradiance_texture_desc);
                if (!gpu_scene.ddgi.irradiance_history_texture)
                {
                    backlog::Post("failed to create ddgi irradiance history texture", backlog::LogLevel::Error);
                    return false;
                }
                gpu_scene.ddgi.irradiance_history_texture->SetName("DDGI Irradiance History Texture");
                if (!device->CreateSubresource(*gpu_scene.ddgi.irradiance_history_texture, ddgi_irradiance_srv_desc, &gpu_scene.ddgi.irradiance_history_texture_srv))
                {
                    backlog::Post("failed to create ddgi irradiance history srv", backlog::LogLevel::Error);
                    gpu_scene.ddgi.irradiance_history_texture = nullptr;
                    return false;
                }

                RHITextureDesc ddgi_visibility_texture_desc = {};
                ddgi_visibility_texture_desc.width = (std::max)(gpu_scene.shader_ddgi_volume.probe_counts.x, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2);
                ddgi_visibility_texture_desc.height = (std::max)(gpu_scene.shader_ddgi_volume.probe_counts.y, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2);
                ddgi_visibility_texture_desc.depth = 1;
                ddgi_visibility_texture_desc.mip_levels = 1;
                ddgi_visibility_texture_desc.array_layers = (std::max)(gpu_scene.shader_ddgi_volume.probe_counts.z, 1u);
                ddgi_visibility_texture_desc.sample_count = 1;
                ddgi_visibility_texture_desc.format = RHIFormat::R16G16B16A16Float;
                ddgi_visibility_texture_desc.usage = RHIResourceUsage::Default;
                ddgi_visibility_texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                gpu_scene.ddgi.visibility_texture = device->CreateTexture(ddgi_visibility_texture_desc);
                if (!gpu_scene.ddgi.visibility_texture)
                {
                    backlog::Post("failed to create ddgi visibility texture", backlog::LogLevel::Error);
                    return false;
                }
                gpu_scene.ddgi.visibility_texture->SetName("DDGI Visibility Texture");

                RHISubresourceDesc ddgi_visibility_srv_desc = {};
                ddgi_visibility_srv_desc.type = RHISubresourceType::ShaderResource;
                ddgi_visibility_srv_desc.format = ddgi_visibility_texture_desc.format;
                ddgi_visibility_srv_desc.first_slice = 0;
                ddgi_visibility_srv_desc.slice_count = ddgi_visibility_texture_desc.array_layers;
                ddgi_visibility_srv_desc.first_mip = 0;
                ddgi_visibility_srv_desc.mip_count = 1;
                if (!device->CreateSubresource(*gpu_scene.ddgi.visibility_texture, ddgi_visibility_srv_desc, &gpu_scene.ddgi.visibility_texture_srv))
                {
                    backlog::Post("failed to create ddgi visibility srv", backlog::LogLevel::Error);
                    gpu_scene.ddgi.visibility_texture = nullptr;
                    return false;
                }

                RHISubresourceDesc ddgi_visibility_uav_desc = {};
                ddgi_visibility_uav_desc.type = RHISubresourceType::UnorderedAccess;
                ddgi_visibility_uav_desc.format = ddgi_visibility_texture_desc.format;
                ddgi_visibility_uav_desc.first_mip = 0;
                ddgi_visibility_uav_desc.mip_count = 1;
                ddgi_visibility_uav_desc.first_slice = 0;
                ddgi_visibility_uav_desc.slice_count = ddgi_visibility_texture_desc.array_layers;
                if (!device->CreateSubresource(*gpu_scene.ddgi.visibility_texture, ddgi_visibility_uav_desc, &gpu_scene.ddgi.visibility_texture_uav))
                {
                    backlog::Post("failed to create ddgi visibility uav", backlog::LogLevel::Error);
                    gpu_scene.ddgi.visibility_texture = nullptr;
                    return false;
                }

                gpu_scene.ddgi.visibility_history_texture = device->CreateTexture(ddgi_visibility_texture_desc);
                if (!gpu_scene.ddgi.visibility_history_texture)
                {
                    backlog::Post("failed to create ddgi visibility history texture", backlog::LogLevel::Error);
                    return false;
                }
                gpu_scene.ddgi.visibility_history_texture->SetName("DDGI Visibility History Texture");
                if (!device->CreateSubresource(*gpu_scene.ddgi.visibility_history_texture, ddgi_visibility_srv_desc, &gpu_scene.ddgi.visibility_history_texture_srv))
                {
                    backlog::Post("failed to create ddgi visibility history srv", backlog::LogLevel::Error);
                    gpu_scene.ddgi.visibility_history_texture = nullptr;
                    return false;
                }

                const uint32 total_probe_count = (std::max)(gpu_scene.shader_ddgi_volume.total_probe_count, 1u);
                RHIBufferDesc ddgi_probe_data_buffer_desc = {};
                ddgi_probe_data_buffer_desc.size = sizeof(float4) * total_probe_count;
                ddgi_probe_data_buffer_desc.usage = RHIResourceUsage::Default;
                ddgi_probe_data_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                gpu_scene.ddgi.probe_data_buffer = device->CreateBuffer(ddgi_probe_data_buffer_desc);
                if (!gpu_scene.ddgi.probe_data_buffer)
                {
                    backlog::Post("failed to create ddgi probe data buffer", backlog::LogLevel::Error);
                    return false;
                }
                gpu_scene.ddgi.probe_data_buffer->SetName("DDGI Probe Data Buffer");

                RHISubresourceDesc ddgi_probe_data_srv_desc = {};
                ddgi_probe_data_srv_desc.type = RHISubresourceType::ShaderResource;
                ddgi_probe_data_srv_desc.buffer_offset = 0;
                ddgi_probe_data_srv_desc.buffer_size = gpu_scene.ddgi.probe_data_buffer->GetDesc().buffer_desc.size;
                ddgi_probe_data_srv_desc.buffer_stride = sizeof(float4);
                if (!device->CreateSubresource(*gpu_scene.ddgi.probe_data_buffer, ddgi_probe_data_srv_desc, &gpu_scene.ddgi.probe_data_buffer_srv))
                {
                    backlog::Post("failed to create ddgi probe data srv", backlog::LogLevel::Error);
                    gpu_scene.ddgi.probe_data_buffer = nullptr;
                    return false;
                }

                RHISubresourceDesc ddgi_probe_data_uav_desc = {};
                ddgi_probe_data_uav_desc.type = RHISubresourceType::UnorderedAccess;
                ddgi_probe_data_uav_desc.buffer_offset = 0;
                ddgi_probe_data_uav_desc.buffer_size = gpu_scene.ddgi.probe_data_buffer->GetDesc().buffer_desc.size;
                ddgi_probe_data_uav_desc.buffer_stride = sizeof(float4);
                if (!device->CreateSubresource(*gpu_scene.ddgi.probe_data_buffer, ddgi_probe_data_uav_desc, &gpu_scene.ddgi.probe_data_buffer_uav))
                {
                    backlog::Post("failed to create ddgi probe data uav", backlog::LogLevel::Error);
                    gpu_scene.ddgi.probe_data_buffer = nullptr;
                    return false;
                }

                gpu_scene.ddgi.probe_data_history_buffer = device->CreateBuffer(ddgi_probe_data_buffer_desc);
                if (!gpu_scene.ddgi.probe_data_history_buffer)
                {
                    backlog::Post("failed to create ddgi probe data history buffer", backlog::LogLevel::Error);
                    return false;
                }
                gpu_scene.ddgi.probe_data_history_buffer->SetName("DDGI Probe Data History Buffer");
                if (!device->CreateSubresource(*gpu_scene.ddgi.probe_data_history_buffer, ddgi_probe_data_srv_desc, &gpu_scene.ddgi.probe_data_history_buffer_srv))
                {
                    backlog::Post("failed to create ddgi probe data history srv", backlog::LogLevel::Error);
                    gpu_scene.ddgi.probe_data_history_buffer = nullptr;
                    return false;
                }

                gpu_scene.ddgi.probe_counts = gpu_scene.shader_ddgi_volume.probe_counts;
                gpu_scene.ddgi.probe_spacing = gpu_scene.shader_ddgi_volume.probe_spacing;
                gpu_scene.ddgi.volume_min = gpu_scene.shader_ddgi_volume.volume_min;
                gpu_scene.ddgi.max_distance = gpu_scene.shader_ddgi_volume.max_distance;
                gpu_scene.ddgi.probe_update_offset = 0;
            }

        }
        else
        {
            // release ddgi resources
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.irradiance_texture));
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.irradiance_history_texture));
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.visibility_texture));
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.visibility_history_texture));
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.probe_data_buffer));
            frame_context.RemoveResourceDeferred(std::move(gpu_scene.ddgi.probe_data_history_buffer));
            gpu_scene.ddgi.irradiance_texture_srv = {};
            gpu_scene.ddgi.irradiance_texture_uav = {};
            gpu_scene.ddgi.irradiance_history_texture_srv = {};
            gpu_scene.ddgi.visibility_texture_srv = {};
            gpu_scene.ddgi.visibility_texture_uav = {};
            gpu_scene.ddgi.visibility_history_texture_srv = {};
            gpu_scene.ddgi.probe_data_buffer_srv = {};
            gpu_scene.ddgi.probe_data_buffer_uav = {};
            gpu_scene.ddgi.probe_data_history_buffer_srv = {};
            gpu_scene.ddgi.probe_counts = { 0, 0, 0 };
            gpu_scene.ddgi.probe_spacing = { 0.0f, 0.0f, 0.0f };
            gpu_scene.ddgi.volume_min = { 0.0f, 0.0f, 0.0f };
            gpu_scene.ddgi.max_distance = 0.0f;
            gpu_scene.ddgi.probe_update_offset = 0;
            gpu_scene.ddgi.history_valid = false;
        }
        return true;
    }

    bool RendererInternal::UploadViewData(FrameContext& frame_context, View& view)
    {
        if (!view.scene || view.camera_entity == INVALID_ENTITY)
        {
            return false;
        }

        View::RenderTargets& targets = view.render_targets;
        const uint32 width = static_cast<uint32>((std::max)(1, view.viewport.width));
        const uint32 height = static_cast<uint32>((std::max)(1, view.viewport.height));

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
                return false;
            }
            view.view_constants.buffer->SetName("View Constants");

            RHISubresourceDesc view_constants_cbv_desc = {};
            view_constants_cbv_desc.type = RHISubresourceType::ConstantBuffer;
            view_constants_cbv_desc.buffer_offset = 0;
            view_constants_cbv_desc.buffer_size = sizeof(ShaderView);
            if (!device->CreateSubresource(*view.view_constants.buffer, view_constants_cbv_desc, &view.view_constants.cbv))
            {
                return false;
            }
        }

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
        targets.depth = frame_graph.CreateTexture(view.viewer_index, "View Depth Buffer", depth_desc);

        RHISubresourceDesc depth_subresource_desc = {};
        depth_subresource_desc.type = RHISubresourceType::DepthStencil;
        depth_subresource_desc.format = depth_desc.format;
        targets.depth_dsv = frame_graph.CreateSubresource(targets.depth, depth_subresource_desc);
        depth_subresource_desc.read_only = true;
        targets.depth_readonly_dsv = frame_graph.CreateSubresource(targets.depth, depth_subresource_desc);
        depth_subresource_desc.read_only = false;
        depth_subresource_desc.type = RHISubresourceType::ShaderResource;
        targets.depth_srv = frame_graph.CreateSubresource(targets.depth, depth_subresource_desc);

        for (uint32 i = 0; i < 2; ++i)
        {
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
            targets.color[i] = frame_graph.CreateTexture(view.viewer_index, i == 0 ? "View Color Buffer" : "View Post Color Buffer", color_desc);

            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.format = color_desc.format;
            srv_desc.first_slice = 0;
            srv_desc.slice_count = 1;
            srv_desc.first_mip = 0;
            srv_desc.mip_count = 1;
            RHISubresourceDesc uav_desc = srv_desc;
            uav_desc.type = RHISubresourceType::UnorderedAccess;
            RHISubresourceDesc rtv_desc = {};
            rtv_desc.type = RHISubresourceType::RenderTarget;
            rtv_desc.format = color_desc.format;

            targets.color_srv[i] = frame_graph.CreateSubresource(targets.color[i], srv_desc);
            targets.color_uav[i] = frame_graph.CreateSubresource(targets.color[i], uav_desc);
            targets.color_rtv[i] = frame_graph.CreateSubresource(targets.color[i], rtv_desc);
        }

        rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();



        // shadow map atlas
        if (view.shadow_resources.shadow_map_atlas_size.x == 0 || view.shadow_resources.shadow_map_atlas_size.y == 0)
        {
            view.shadow_resources.atlas = invalid_frame_resource;
            view.shadow_resources.atlas_dsv = {};
            view.shadow_resources.atlas_srv = {};
        }
        else
        {
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
            view.shadow_resources.atlas = frame_graph.CreateTexture(view.viewer_index, "Shadow Map Atlas", shadow_map_atlas_desc);

            RHISubresourceDesc shadow_map_atlas_subresource_desc = {};
            shadow_map_atlas_subresource_desc.type = RHISubresourceType::DepthStencil;
            shadow_map_atlas_subresource_desc.format = shadow_map_atlas_desc.format;
            view.shadow_resources.atlas_dsv = frame_graph.CreateSubresource(view.shadow_resources.atlas, shadow_map_atlas_subresource_desc);
            shadow_map_atlas_subresource_desc.type = RHISubresourceType::ShaderResource;
            view.shadow_resources.atlas_srv = frame_graph.CreateSubresource(view.shadow_resources.atlas, shadow_map_atlas_subresource_desc);
        }

        // per-view shadow gpu buffers
        const Vector<ShaderShadowCascade>& shader_shadow_cascades = view.shadow_resources.shader_shadow_cascades;
        const Size required_shadow_cascade_buffer_size = shader_shadow_cascades.size() * sizeof(ShaderShadowCascade);
        const Size required_shadow_slice_buffer_size = view.shadow_resources.light_shadow_slices.size() * sizeof(uint32);

        if (required_shadow_cascade_buffer_size == 0)
        {
            view.shadow_resources.cascade_buffer = invalid_frame_resource;
            view.shadow_resources.cascade_srv = {};
        }
        else
        {
            RHIBufferDesc cascade_buffer_desc = {};
            cascade_buffer_desc.size = required_shadow_cascade_buffer_size;
            cascade_buffer_desc.usage = RHIResourceUsage::Default;
            cascade_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
            view.shadow_resources.cascade_buffer = frame_graph.CreateBuffer(view.viewer_index, "View Shadow Cascade Buffer", cascade_buffer_desc);

            RHISubresourceDesc cascade_srv_desc = {};
            cascade_srv_desc.type = RHISubresourceType::ShaderResource;
            cascade_srv_desc.buffer_offset = 0;
            cascade_srv_desc.buffer_size = cascade_buffer_desc.size;
            cascade_srv_desc.buffer_stride = sizeof(ShaderShadowCascade);
            view.shadow_resources.cascade_srv = frame_graph.CreateSubresource(view.shadow_resources.cascade_buffer, cascade_srv_desc);

            if (!frame_graph.QueueBufferUpload(view.shadow_resources.cascade_buffer, shader_shadow_cascades.data(), required_shadow_cascade_buffer_size))
            {
                return false;
            }
        }
        if (required_shadow_slice_buffer_size == 0)
        {
            view.shadow_resources.light_slice_buffer = invalid_frame_resource;
            view.shadow_resources.light_slice_srv = {};
        }
        else
        {
            RHIBufferDesc shadow_slice_buffer_desc = {};
            shadow_slice_buffer_desc.size = required_shadow_slice_buffer_size;
            shadow_slice_buffer_desc.usage = RHIResourceUsage::Default;
            shadow_slice_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
            view.shadow_resources.light_slice_buffer = frame_graph.CreateBuffer(view.viewer_index, "View Light Shadow Slice Buffer", shadow_slice_buffer_desc);

            RHISubresourceDesc shadow_slice_srv_desc = {};
            shadow_slice_srv_desc.type = RHISubresourceType::ShaderResource;
            shadow_slice_srv_desc.buffer_offset = 0;
            shadow_slice_srv_desc.buffer_size = shadow_slice_buffer_desc.size;
            shadow_slice_srv_desc.buffer_stride = sizeof(uint32);
            view.shadow_resources.light_slice_srv = frame_graph.CreateSubresource(view.shadow_resources.light_slice_buffer, shadow_slice_srv_desc);

            if (!frame_graph.QueueBufferUpload(view.shadow_resources.light_slice_buffer, view.shadow_resources.light_shadow_slices.data(), required_shadow_slice_buffer_size))
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
                view.instance_resources.sort_buffer = invalid_frame_resource;
                view.instance_resources.sort_srv = {};
            }
            else
            {
                RHIBufferDesc desc = {};
                desc.size = required_sort_buffer_size;
                desc.usage = RHIResourceUsage::Default;
                desc.bind_flags = RHIBindFlags::ShaderResource;
                view.instance_resources.sort_buffer = frame_graph.CreateBuffer(view.viewer_index, "Shader Instance Sort Default Buffer", desc);

                RHISubresourceDesc srv_desc = {};
                srv_desc.type = RHISubresourceType::ShaderResource;
                srv_desc.buffer_offset = 0;
                srv_desc.buffer_size = desc.size;
                srv_desc.buffer_stride = sizeof(uint32);
                view.instance_resources.sort_srv = frame_graph.CreateSubresource(view.instance_resources.sort_buffer, srv_desc);

                sort_upload_scratch.resize(opaque_count + transparent_count + shadow_caster_count);
                uint32* mapped = sort_upload_scratch.data();
                for (uint32 i = 0; i < opaque_count; ++i)
					mapped[i] = opaque[view.sorted_opaque_indices[i]].push_constants.draw_offset; // renderable index -> ShaderInstance index, all submeshes share the same ShaderInstance index
                for (uint32 i = 0; i < transparent_count; ++i)
                    mapped[opaque_count + i] = transparent[view.sorted_transparent_indices[i]].push_constants.draw_offset;
                for (uint32 i = 0; i < shadow_caster_count; ++i)
                    mapped[opaque_count + transparent_count + i] = opaque[view.sorted_shadow_caster_indices[i]].push_constants.draw_offset;

                frame_graph.QueueBufferUpload(view.instance_resources.sort_buffer, sort_upload_scratch.data(), required_sort_buffer_size);
            }
        }

        // per-view light culling resources
        view.light_resources.forward_index_buffer = invalid_frame_resource;
        view.light_resources.forward_index_srv = {};
        view.light_resources.cluster_light_count_buffer = invalid_frame_resource;
        view.light_resources.cluster_light_count_srv = {};
        view.light_resources.cluster_light_count_uav = {};
        view.light_resources.cluster_light_offset_buffer = invalid_frame_resource;
        view.light_resources.cluster_light_offset_srv = {};
        view.light_resources.cluster_light_offset_uav = {};
        view.light_resources.cluster_light_index_buffer = invalid_frame_resource;
        view.light_resources.cluster_light_index_srv = {};
        view.light_resources.cluster_light_index_uav = {};

        if (view.render_path_type == RenderPathType::ForwardPlus)
        {
            const uint32 tiles_x = (static_cast<uint32>(view.viewport.width) + LIGHTCULL_TILE_SIZE - 1) / LIGHTCULL_TILE_SIZE;
            const uint32 tiles_y = (static_cast<uint32>(view.viewport.height) + LIGHTCULL_TILE_SIZE - 1) / LIGHTCULL_TILE_SIZE;
            const int cluster_depth_slices_requested = r_cluster_depth_slices.GetInt();
            const uint32 depth_slices = cluster_depth_slices_requested < 1 ? 1u : (std::min)(static_cast<uint32>(cluster_depth_slices_requested), static_cast<uint32>(MAX_DEPTH_SLICES));
            if (tiles_x > 0 && tiles_y > 0)
            {
                const uint32 cluster_count = tiles_x * tiles_y * depth_slices;

                RHIBufferDesc grid_desc = {};
                grid_desc.size = static_cast<Size>(cluster_count) * sizeof(uint32);
                grid_desc.usage = RHIResourceUsage::Default;
                grid_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                const FrameResourceId count_resource = frame_graph.CreateBuffer(view.viewer_index, "Cluster Light Count", grid_desc);
                const FrameResourceId offset_resource = frame_graph.CreateBuffer(view.viewer_index, "Cluster Light Offset", grid_desc);

                RHIBufferDesc index_desc = {};
                index_desc.size = static_cast<Size>(cluster_count) * MAX_LIGHTS_PER_CLUSTER * sizeof(uint32);
                index_desc.usage = RHIResourceUsage::Default;
                index_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                const FrameResourceId index_resource = frame_graph.CreateBuffer(view.viewer_index, "Cluster Light Index List", index_desc);

                view.light_resources.cluster_light_count_buffer = count_resource;
                view.light_resources.cluster_light_offset_buffer = offset_resource;
                view.light_resources.cluster_light_index_buffer = index_resource;

                if (view.light_resources.cluster_light_count_buffer != invalid_frame_resource && view.light_resources.cluster_light_offset_buffer != invalid_frame_resource && view.light_resources.cluster_light_index_buffer != invalid_frame_resource)
                {
                    RHISubresourceDesc grid_uav_desc = {};
                    grid_uav_desc.type = RHISubresourceType::UnorderedAccess;
                    grid_uav_desc.buffer_offset = 0;
                    grid_uav_desc.buffer_size = grid_desc.size;
                    grid_uav_desc.buffer_stride = sizeof(uint32);
                    RHISubresourceDesc grid_srv_desc = grid_uav_desc;
                    grid_srv_desc.type = RHISubresourceType::ShaderResource;
                    view.light_resources.cluster_light_count_uav = frame_graph.CreateSubresource(count_resource, grid_uav_desc);
                    view.light_resources.cluster_light_count_srv = frame_graph.CreateSubresource(count_resource, grid_srv_desc);
                    view.light_resources.cluster_light_offset_uav = frame_graph.CreateSubresource(offset_resource, grid_uav_desc);
                    view.light_resources.cluster_light_offset_srv = frame_graph.CreateSubresource(offset_resource, grid_srv_desc);

                    RHISubresourceDesc index_uav_desc = {};
                    index_uav_desc.type = RHISubresourceType::UnorderedAccess;
                    index_uav_desc.buffer_offset = 0;
                    index_uav_desc.buffer_size = index_desc.size;
                    index_uav_desc.buffer_stride = sizeof(uint32);
                    RHISubresourceDesc index_srv_desc = index_uav_desc;
                    index_srv_desc.type = RHISubresourceType::ShaderResource;
                    view.light_resources.cluster_light_index_uav = frame_graph.CreateSubresource(index_resource, index_uav_desc);
                    view.light_resources.cluster_light_index_srv = frame_graph.CreateSubresource(index_resource, index_srv_desc);

                    view.light_resources.cluster_dims = { tiles_x, tiles_y };
                    view.light_resources.depth_slice_count = depth_slices;
                }
            }
        }
        else if (view.render_path_type == RenderPathType::Forward)
        {
            View::LightResources& light_resources = view.light_resources;
            light_resources.forward_light_count = 0;

            const Vector<uint32>& visible_lights = light_resources.visible_forward_lights;
            if (!visible_lights.empty())
            {
                const Size forward_index_size = static_cast<Size>(NUM_MAX_LIGHTS_FORWARD_RENDERING) * sizeof(uint32);
                RHIBufferDesc forward_index_desc = {};
                forward_index_desc.size = forward_index_size;
                forward_index_desc.usage = RHIResourceUsage::Default;
                forward_index_desc.bind_flags = RHIBindFlags::ShaderResource;
                light_resources.forward_index_buffer = frame_graph.CreateBuffer(view.viewer_index, "Forward Light Index Buffer", forward_index_desc);

                RHISubresourceDesc forward_index_srv_desc = {};
                forward_index_srv_desc.type = RHISubresourceType::ShaderResource;
                forward_index_srv_desc.buffer_offset = 0;
                forward_index_srv_desc.buffer_size = forward_index_size;
                forward_index_srv_desc.buffer_stride = sizeof(uint32);
                light_resources.forward_index_srv = frame_graph.CreateSubresource(light_resources.forward_index_buffer, forward_index_srv_desc);

                frame_graph.QueueBufferUpload(light_resources.forward_index_buffer, visible_lights.data(), visible_lights.size() * sizeof(uint32));

                light_resources.forward_light_count = static_cast<uint32>(visible_lights.size());
            }
        }

        view.water_resources.tile_buffer = invalid_frame_resource;
        view.water_resources.tile_srv = {};
        if (!view.water_resources.tiles.empty())
        {
            const Size tile_buffer_size = view.water_resources.tiles.size() * sizeof(ShaderWaterTile);
            RHIBufferDesc tile_desc = {};
            tile_desc.size = tile_buffer_size;
            tile_desc.usage = RHIResourceUsage::Default;
            tile_desc.bind_flags = RHIBindFlags::ShaderResource;
            view.water_resources.tile_buffer = frame_graph.CreateBuffer(view.viewer_index, "Water Tile Buffer", tile_desc);

            RHISubresourceDesc tile_srv_desc = {};
            tile_srv_desc.type = RHISubresourceType::ShaderResource;
            tile_srv_desc.buffer_offset = 0;
            tile_srv_desc.buffer_size = tile_buffer_size;
            tile_srv_desc.buffer_stride = sizeof(ShaderWaterTile);
            view.water_resources.tile_srv = frame_graph.CreateSubresource(view.water_resources.tile_buffer, tile_srv_desc);

            frame_graph.QueueBufferUpload(view.water_resources.tile_buffer, view.water_resources.tiles.data(), tile_buffer_size);
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
            frame_context.RemoveResourceDeferred(std::move(view.ddgi_debug_resources.probe_data_readback_buffer));
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

        RHIResource* bound_index_buffer = nullptr;
        uint32 bound_index_buffer_offset = 0;
        RHIPrimitiveTopology bound_topology = RHIPrimitiveTopology::TriangleList;
        bool has_bound_topology = false;

		// for instanced rendering, submesh1 * N    FLUSH    submesh2 * N ...
        auto flush_batch = [&](const Vector<Renderable>& renderables, const Vector<uint32>& sort_indices, uint32 sort_buffer_base, uint32 start, uint32 size)
        {
            if (size == 0)
                return;
            const auto& first = renderables[sort_indices[start]];
            ObjectPushConstants push = first.push_constants;
            push.draw_offset = sort_buffer_base + start; // starting offset of sort_indices

            if (bound_index_buffer != first.index_buffer || bound_index_buffer_offset != first.index_buffer_offset)
            {
                command_list.SetIndexBuffer(*first.index_buffer, sizeof(uint32), first.index_buffer_offset, first.index_buffer_size);
                bound_index_buffer = first.index_buffer;
                bound_index_buffer_offset = first.index_buffer_offset;
            }

            const RHIPrimitiveTopology topology = ToRHIPrimitiveTopology(first.primitive_topology);
            if (!has_bound_topology || bound_topology != topology)
            {
                command_list.SetPrimitiveTopology(topology);
                bound_topology = topology;
                has_bound_topology = true;
            }

            command_list.PushConstants(RHIShaderStage::Vertex, &push, sizeof(ObjectPushConstants), 0);
            command_list.DrawIndexed(first.index_count, size, first.first_index, 0, 0);
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

                flush_batch(gpu_scene.transparent_renderables, view.sorted_transparent_indices, sort_buffer_base, i, 1);
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
                    command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_buffer_offset, renderable.index_buffer_size);
                    command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                    command_list.PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                    command_list.DrawIndexed(renderable.index_count, 1, renderable.first_index, 0, 0);
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
                    command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_buffer_offset, renderable.index_buffer_size);
                    command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                    command_list.PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                    command_list.DrawIndexed(renderable.index_count, 1, renderable.first_index, 0, 0);
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
        frame_graph.Initialize(device);
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
        frame_delta_seconds = delta_time;

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
            uint64 completed_component_mask = 0;
            utils::FlushEnqueuedResourceUploads(*device, static_cast<uint32>(r_upload_budget.GetInt()), &completed_component_mask);
            upload_completed_component_mask |= completed_component_mask;
        }

        CreateBackBufferSubresources();

        RHISubresourceBinding back_buffer_binding = {};
        if (!GetCurrentBackBufferBinding(back_buffer_binding))
        {
            return;
        }

        frame_graph.BeginFrame(frame_context);
        const FrameResourceId back_buffer_id = frame_graph.Import(*back_buffer_binding.resource);
        frame_graph.MarkNoCull(back_buffer_id);
        frame_graph.AddPass("Clear Back Buffer",
            { { back_buffer_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::Write } },
            [this, back_buffer_binding](const FrameGraphPassContext& pass_context)
        {
            pass_context.command_list->ClearRenderTarget(back_buffer_binding, clear_color);
        });
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
        const RHIResourceState history_state = gpu_scene.ddgi.history_valid ? RHIResourceState::ShaderRead : RHIResourceState::Undefined;

        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_texture, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_texture, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
        if (gpu_scene.ddgi.history_valid)
        {
            command_list.TransitionResource(*gpu_scene.ddgi.irradiance_history_texture, RHIResourceState::Undefined, RHIResourceState::ShaderRead);
            command_list.TransitionResource(*gpu_scene.ddgi.visibility_history_texture, RHIResourceState::Undefined, RHIResourceState::ShaderRead);
            command_list.TransitionResource(*gpu_scene.ddgi.probe_data_history_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderRead);
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

        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_texture, RHIResourceState::ShaderWrite, RHIResourceState::CopySource);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_texture, RHIResourceState::ShaderWrite, RHIResourceState::CopySource);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_buffer, RHIResourceState::ShaderWrite, RHIResourceState::CopySource);
        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_history_texture, history_state, RHIResourceState::CopyDest);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_history_texture, history_state, RHIResourceState::CopyDest);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_history_buffer, history_state, RHIResourceState::CopyDest);

        command_list.CopyResource(*gpu_scene.ddgi.irradiance_history_texture, *gpu_scene.ddgi.irradiance_texture);
        command_list.CopyResource(*gpu_scene.ddgi.visibility_history_texture, *gpu_scene.ddgi.visibility_texture);
        command_list.CopyResource(*gpu_scene.ddgi.probe_data_history_buffer, *gpu_scene.ddgi.probe_data_buffer);
        if (view.ddgi_debug_resources.probe_data_readback_buffer)
        {
            command_list.CopyBuffer(*view.ddgi_debug_resources.probe_data_readback_buffer, 0, *gpu_scene.ddgi.probe_data_buffer, 0, gpu_scene.ddgi.probe_data_buffer->GetDesc().buffer_desc.size);
        }

        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_texture, RHIResourceState::CopySource, RHIResourceState::Undefined);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_texture, RHIResourceState::CopySource, RHIResourceState::Undefined);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_buffer, RHIResourceState::CopySource, RHIResourceState::Undefined);
        command_list.TransitionResource(*gpu_scene.ddgi.irradiance_history_texture, RHIResourceState::CopyDest, RHIResourceState::Undefined);
        command_list.TransitionResource(*gpu_scene.ddgi.visibility_history_texture, RHIResourceState::CopyDest, RHIResourceState::Undefined);
        command_list.TransitionResource(*gpu_scene.ddgi.probe_data_history_buffer, RHIResourceState::CopyDest, RHIResourceState::Undefined);
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

        if ((view.show_flags & Show_Vehicles) != 0)
        {
            auto vehicle_array = view.scene->GetComponentArray<ecs::VehicleComponent>().get();
            auto transform_array = view.scene->GetComponentArray<ecs::TransformComponent>().get();
            if (vehicle_array && transform_array)
            {
                for (Size vehicle_index = 0; vehicle_index < vehicle_array->GetSize(); ++vehicle_index)
                {
                    const ecs::Entity entity = vehicle_array->index_to_entity[vehicle_index];
                    if (!transform_array->HasData(entity))
                    {
                        continue;
                    }

                    const ecs::VehicleComponent& vehicle = vehicle_array->data[vehicle_index];
                    if (!vehicle.IsEnabled())
                    {
                        continue;
                    }

                    const DirectX::XMMATRIX world = transform_array->GetData(entity).GetWorldTransform();
                    for (const ecs::VehicleWheel& wheel : vehicle.wheels)
                    {
                        const DirectX::XMVECTOR attachment = DirectX::XMVector3TransformCoord(DirectX::XMLoadFloat3(&wheel.attachment_position), world);
                        float3 attachment_position = {};
                        DirectX::XMStoreFloat3(&attachment_position, attachment);

                        const uint32 color = wheel.has_contact ? debugdraw::color::vehicle_wheel_contact : debugdraw::color::vehicle_wheel;
                        debugdraw::Line3D(attachment_position, wheel.world_position, color);
                        debugdraw::Sphere3D(wheel.world_position, wheel.radius, color);
                    }
                }
            }
        }

        if ((view.show_flags & Show_Occlusion) != 0 && view.occlusion_resources.active)
        {
            for (uint32 renderable_index : view.occlusion_query_indices)
            {
                const Renderable& renderable = gpu_scene.opaque_renderables[renderable_index];
                const View::OcclusionResources::RenderableKey key = { renderable.entity, renderable.push_constants.geometry_index };
                const auto entry = view.occlusion_resources.visibility.find(key);
                if (entry == view.occlusion_resources.visibility.end() || !entry->second.IsOccluded())
                {
                    continue;
                }
                debugdraw::Box3D(renderable.aabb.min, renderable.aabb.max, debugdraw::color::occluded);
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
            frame_context.RemoveResourceDeferred(std::move(debug_3d_buffer));
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

        if (!UpdateDefaultBuffer(frame_context, *debug_3d_buffer, line_vertices.data(), required_buffer_size, RHIResourceState::Undefined, 0, command_list))
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

        RHISubresourceBinding back_buffer_binding = {};
        if (!GetCurrentBackBufferBinding(back_buffer_binding))
        {
            debugdraw::Clear2D();
            return;
        }

        const FrameResourceId back_buffer_id = frame_graph.Import(*back_buffer_binding.resource);
        frame_graph.AddPass("DebugDraw2D Pass",
            { { back_buffer_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite } },
            [this](const FrameGraphPassContext& pass_context)
        {
            auto gpu_range = profiler::ScopedRangeGPU("DebugDraw2D Pass", (*pass_context.command_list));
            DrawDebug2D((*pass_context.command_list));
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

        if (upload_completed_component_mask != 0)
        {
            scene.MarkGpuDirty(upload_completed_component_mask);
        }

        if (scene.GetUpdateIndex() != gpu_scene.synced_index)
        {
            UploadSceneData(frame_context, scene, gpu_scene);
            gpu_scene.synced_index = scene.GetUpdateIndex();
        }

        {
            auto cpu_range = profiler::ScopedRangeCPU("Upload View Data");
            if (!UploadViewData(frame_context, view))
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

                const FrameResourceId brdf_lut_id = frame_graph.Import(*brdf_lut);
                frame_graph.MarkNoCull(brdf_lut_id);
                frame_graph.AddPass("Integrate BRDF", { { brdf_lut_id, RHIResourceState::Undefined, FrameResourceAccess::Type::Write } }, [this, brdf_integration_pipeline](const FrameGraphPassContext& pass_context)
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Integrate BRDF", (*pass_context.command_list));
                    pass_context.command_list->TransitionResource(*brdf_lut, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
                    pass_context.command_list->SetComputePipeline(*brdf_integration_pipeline);
                    BRDFIntegrationPushConstants brdf_push = {};
                    brdf_push.Init();
                    brdf_push.output_descriptor = static_cast<uint32>(brdf_lut_uav.descriptor_index);
                    pass_context.command_list->PushConstants(RHIShaderStage::Compute, &brdf_push, sizeof(brdf_push), 0);
                    const uint32 brdf_group_count = (brdf_lut_resolution + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D;
                    pass_context.command_list->Dispatch(brdf_group_count, brdf_group_count, 1u);
                    pass_context.command_list->UAVBarrier(*brdf_lut);
                    pass_context.command_list->TransitionResource(*brdf_lut, RHIResourceState::ShaderWrite, RHIResourceState::Undefined);
                });
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
        
        // TODO : check fence
        if (ecs::CameraComponent* exposure_camera = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity))
        {
            View::ExposureResources& exposure = view.exposure_resources;
            if (exposure_camera->IsAutoExposure() && exposure.luminance_readback_buffer)
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
                    float target = exposure_camera->exposure_multiplier * (ecs::CameraComponent::auto_exposure_target / measured);
                    const float exposure_low = ecs::CameraComponent::ExposureFromEV100(exposure_camera->auto_exposure_max_ev);
                    const float exposure_high = ecs::CameraComponent::ExposureFromEV100(exposure_camera->auto_exposure_min_ev);
                    target = (std::min)((std::max)(target, exposure_low), exposure_high);
                    const float lerp_factor = (std::min)(1.0f, (std::max)(0.0f, exposure_camera->auto_exposure_speed * 0.02f));
                    exposure_camera->exposure_multiplier += (target - exposure_camera->exposure_multiplier) * lerp_factor;
                }
            }
        }

        {
            if (!UpdateFrameConstants(frame_context, view))
            {
                return false;
            }
        }

        if (gpu_scene.sky_lighting.capture_texture)
        {
            const FrameResourceId sky_capture_id = frame_graph.Import(*gpu_scene.sky_lighting.capture_texture);
            frame_graph.MarkNoCull(sky_capture_id);
            frame_graph.AddPass("Update Sky Capture", { { sky_capture_id, RHIResourceState::Undefined, FrameResourceAccess::Type::ReadWrite } }, [this, &gpu_scene](const FrameGraphPassContext& pass_context)
        {
                UpdateSkyCapture(gpu_scene, (*pass_context.command_list));
            });
        }

        const ecs::Scene::WaterSimulationState& water_simulation = scene.GetWaterSimulation();
        if (water_simulation.pending_steps > 0
            && !gpu_scene.water.zone_simulations.empty()
            && gpu_scene.water.zone_buffer.srv.IsValid()
            && gpu_scene.water.simulated_index != scene.GetUpdateIndex())
        {
            GraphicsPipelineHash splat_pipeline_hash = {};
            splat_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::WaterRippleSplatPass);
            splat_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            splat_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            splat_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            splat_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
            splat_pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Transparent);
            RHIPipeline* splat_pipeline = shader_library.GetPipeline(splat_pipeline_hash);

            if (RHIPipeline* ripple_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSWaterRippleStep)))
            {
                const uint32 step_count = water_simulation.pending_steps;
                const uint64 first_step_index = water_simulation.step_count - step_count;
                const uint32 zone_buffer_descriptor = static_cast<uint32>(gpu_scene.water.zone_buffer.srv.descriptor_index);
                const float step_seconds = scene.GetPhysicsWorld() ? scene.GetPhysicsWorld()->GetFixedStepSeconds() : 0.0f;

                Vector<FrameResourceAccess> ripple_accesses;
                for (const GPUScene::WaterResources::ZoneSimulation& zone_simulation : gpu_scene.water.zone_simulations)
                {
                    if (!zone_simulation.height_texture[0] || !zone_simulation.height_texture[1] || !zone_simulation.wetness_texture)
                    {
                        continue;
                    }
                    for (uint32 i = 0; i < 2; ++i)
                    {
                        const FrameResourceId height_id = frame_graph.Import(*zone_simulation.height_texture[i]);
                        frame_graph.MarkNoCull(height_id);
                        ripple_accesses.push_back({ height_id, RHIResourceState::Undefined, FrameResourceAccess::Type::ReadWrite });
                    }
                    const FrameResourceId wetness_id = frame_graph.Import(*zone_simulation.wetness_texture);
                    frame_graph.MarkNoCull(wetness_id);
                    ripple_accesses.push_back({ wetness_id, RHIResourceState::Undefined, FrameResourceAccess::Type::ReadWrite });
                }

                if (!ripple_accesses.empty())
                {
                    const uint32 injection_count = (std::min)(static_cast<uint32>(gpu_scene.water.injections.size()),
                        static_cast<uint32>(water_ripple_max_injections));
                    FrameResourceId injection_id = invalid_frame_resource;
                    RHISubresourceHandle injection_srv = {};
                    if (injection_count > 0)
                    {
                        RHIBufferDesc injection_desc = {};
                        injection_desc.size = sizeof(ShaderWaterRipple) * injection_count;
                        injection_desc.usage = RHIResourceUsage::Default;
                        injection_desc.bind_flags = RHIBindFlags::ShaderResource;
                        injection_id = frame_graph.CreateBuffer(view.viewer_index, "Water Ripple Injections", injection_desc);

                        RHISubresourceDesc injection_srv_desc = {};
                        injection_srv_desc.type = RHISubresourceType::ShaderResource;
                        injection_srv_desc.buffer_offset = 0;
                        injection_srv_desc.buffer_size = injection_desc.size;
                        injection_srv_desc.buffer_stride = sizeof(ShaderWaterRipple);
                        injection_srv = frame_graph.CreateSubresource(injection_id, injection_srv_desc);
                        frame_graph.QueueBufferUpload(injection_id, gpu_scene.water.injections.data(), injection_desc.size);

                        ripple_accesses.push_back({ injection_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                    }

                    gpu_scene.water.simulated_index = scene.GetUpdateIndex();
                    frame_graph.AddPass("Water Ripple", std::move(ripple_accesses),
                        [&gpu_scene, ripple_pipeline, splat_pipeline, step_count, first_step_index, zone_buffer_descriptor,
                         step_seconds, injection_count, injection_srv](const FrameGraphPassContext& pass_context)
                    {
                        auto gpu_range = profiler::ScopedRangeGPU("Water Ripple", (*pass_context.command_list));
                        RHICommandList* command_list = pass_context.command_list;

                        if (splat_pipeline && injection_count > 0)
                        {
                            command_list->SetGraphicsPipeline(*splat_pipeline);
                            command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                            for (Size zone_index = 0; zone_index < gpu_scene.water.zone_simulations.size(); ++zone_index)
                            {
                                const GPUScene::WaterResources::ZoneSimulation& zone_simulation = gpu_scene.water.zone_simulations[zone_index];
                                const ShaderWaterZone& zone = gpu_scene.water.shader_zones[zone_index];
                                if (!zone_simulation.height_texture[0] || !zone_simulation.height_texture[1] || zone.injection_count == 0)
                                {
                                    continue;
                                }

                                const uint32 splat_index = static_cast<uint32>(first_step_index % 2ull);
                                RHIResource& splat_target = *zone_simulation.height_texture[splat_index];
                                command_list->TransitionResource(splat_target, RHIResourceState::Undefined, RHIResourceState::RenderTarget);

                                const RHISubresourceBinding splat_binding = { &splat_target, zone_simulation.height_rtv[splat_index] };
                                const float splat_resolution = static_cast<float>(zone.ripple_resolution);
                                command_list->SetRenderTargets({ splat_binding }, nullptr);
                                command_list->SetViewport({ 0.0f, 0.0f, splat_resolution, splat_resolution, 0.0f, 1.0f });
                                command_list->SetScissor({ 0, 0, static_cast<int32>(zone.ripple_resolution), static_cast<int32>(zone.ripple_resolution) });

                                WaterRippleSplatPushConstants splat_push = {};
                                splat_push.Init();
                                splat_push.zone_buffer_descriptor = zone_buffer_descriptor;
                                splat_push.zone_index = static_cast<uint32>(zone_index);
                                splat_push.injection_descriptor = static_cast<uint32>(injection_srv.descriptor_index);
                                command_list->PushConstants(RHIShaderStage::Vertex, &splat_push, sizeof(WaterRippleSplatPushConstants), 0);
                                command_list->Draw(6, zone.injection_count, 0, 0);

                                command_list->TransitionResource(splat_target, RHIResourceState::RenderTarget, RHIResourceState::Undefined);
                            }
                        }

                        command_list->SetComputePipeline(*ripple_pipeline);
                        for (Size zone_index = 0; zone_index < gpu_scene.water.zone_simulations.size(); ++zone_index)
                        {
                            const GPUScene::WaterResources::ZoneSimulation& zone_simulation = gpu_scene.water.zone_simulations[zone_index];
                            if (!zone_simulation.height_texture[0] || !zone_simulation.height_texture[1] || !zone_simulation.wetness_texture)
                            {
                                continue;
                            }
                            const ShaderWaterZone& zone = gpu_scene.water.shader_zones[zone_index];
                            const uint32 group_count = (zone.ripple_resolution + WATER_RIPPLE_GROUP_SIZE - 1) / WATER_RIPPLE_GROUP_SIZE;

                            for (uint32 i = 0; i < 2; ++i)
                            {
                                command_list->TransitionResource(*zone_simulation.height_texture[i], RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
                            }
                            command_list->TransitionResource(*zone_simulation.wetness_texture, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);

                            for (uint32 step = 0; step < step_count; ++step)
                            {
                                const uint64 step_index = first_step_index + step;
                                const uint32 height_previous = static_cast<uint32>(step_index % 2ull);
                                const uint32 height_current = static_cast<uint32>((step_index + 1ull) % 2ull);

                                WaterRippleStepPushConstants ripple_push = {};
                                ripple_push.Init();
                                ripple_push.zone_buffer_descriptor = zone_buffer_descriptor;
                                ripple_push.zone_index = static_cast<uint32>(zone_index);
                                ripple_push.step_seconds = step_seconds;
                                ripple_push.height_current_descriptor = static_cast<uint32>(zone_simulation.height_uav[height_current].descriptor_index);
                                ripple_push.height_previous_descriptor = static_cast<uint32>(zone_simulation.height_uav[height_previous].descriptor_index);
                                ripple_push.wetness_descriptor = static_cast<uint32>(zone_simulation.wetness_uav.descriptor_index);
                                command_list->PushConstants(RHIShaderStage::Compute, &ripple_push, sizeof(WaterRippleStepPushConstants), 0);

                                command_list->Dispatch(group_count, group_count, 1);
                                command_list->UAVBarrier(*zone_simulation.height_texture[height_current]);
                                command_list->UAVBarrier(*zone_simulation.wetness_texture);
                            }

                            command_list->TransitionResource(*zone_simulation.wetness_texture, RHIResourceState::ShaderWrite, RHIResourceState::Undefined);
                            for (uint32 i = 0; i < 2; ++i)
                            {
                                command_list->TransitionResource(*zone_simulation.height_texture[i], RHIResourceState::ShaderWrite, RHIResourceState::Undefined);
                            }
                        }
                    });
                }
            }
        }
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
            command_list.TransitionResource(*sky_lighting.capture_texture, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
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
            command_list.TransitionResource(*sky_lighting.capture_texture, RHIResourceState::ShaderWrite, RHIResourceState::Undefined);

            sky_lighting.bake_step = sky_lighting.valid ? 1u : sky_cube_face_count;
            sky_lighting.signature = signature;
            sky_lighting.pending_irradiance_face = sky_lighting.irradiance_texture ? 0 : static_cast<int32>(sky_cube_face_count);
            sky_lighting.pending_specular_mip = sky_lighting.specular_texture ? 0 : static_cast<int32>(sky_specular_mip_count);
        }

        if (sky_lighting.pending_irradiance_face < static_cast<int32>(sky_cube_face_count))
        {
            const uint32 face_offset = static_cast<uint32>(sky_lighting.pending_irradiance_face);
            const uint32 face_count = std::min(sky_lighting.bake_step, sky_cube_face_count - face_offset);

            command_list.TransitionResource(*sky_lighting.irradiance_texture, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
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
            command_list.TransitionResource(*sky_lighting.irradiance_texture, RHIResourceState::ShaderWrite, RHIResourceState::Undefined);

            sky_lighting.pending_irradiance_face = static_cast<int32>(face_offset + face_count);
        }

        if (sky_lighting.pending_specular_mip < static_cast<int32>(sky_specular_mip_count))
        {
            const uint32 first_mip = static_cast<uint32>(sky_lighting.pending_specular_mip);
            const uint32 mip_count = std::min(sky_lighting.bake_step, sky_specular_mip_count - first_mip);
            for (uint32 mip = first_mip; mip < first_mip + mip_count; ++mip)
            {
                const uint32 mip_resolution = sky_specular_resolution >> mip;

                command_list.TransitionResource(*sky_lighting.specular_texture, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
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
                command_list.TransitionResource(*sky_lighting.specular_texture, RHIResourceState::ShaderWrite, RHIResourceState::Undefined);
            }

            sky_lighting.pending_specular_mip = static_cast<int32>(first_mip + mip_count);
        }

        command_list.EndEvent();
        sky_lighting.valid = true;
    }

    FrameGraph& RendererInternal::GetFrameGraph()
    {
        return frame_graph;
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


    void RendererInternal::RenderForwardPath(View& view)
    {
        if (!view.scene || view.camera_entity == ecs::INVALID_ENTITY || !current_window)
            return;

        FrameContext& frame_context = GetFrameContext();
        rendering::GPUScene& gpu_scene = view.scene->GetGPUScene();

        RHISubresourceBinding back_buffer_binding = {};
        if (!GetCurrentBackBufferBinding(back_buffer_binding))
        {
            return;
        }

        View::RenderTargets& targets = view.render_targets;
        View::ExposureResources& exposure = view.exposure_resources;
        if (targets.depth == invalid_frame_resource
            || targets.color[0] == invalid_frame_resource
            || targets.color[1] == invalid_frame_resource)
        {
            return;
        }

        // The scene always renders into the view's color[0]; the post chain ping-pongs between the
        // view targets and the result is composited into the backbuffer at the view's viewport rect.
        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_cbv;

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
        frame_graph.SetDefaultViewport(viewport, scissor);

        const FrameResourceId view_constants_id = frame_graph.Import(*view.view_constants.buffer);
        const FrameResourceAccess view_constants_read = { view_constants_id, RHIResourceState::ConstantBuffer, FrameResourceAccess::Type::Read };
        const FrameResourceAccess view_constants_write = { view_constants_id, RHIResourceState::ConstantBuffer, FrameResourceAccess::Type::ReadWrite };
        const FrameResourceAccess sort_buffer_read = { view.instance_resources.sort_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read };

        if (gpu_scene.ddgi.irradiance_texture)
        {
            const FrameResourceId ddgi_irradiance_id = frame_graph.Import(*gpu_scene.ddgi.irradiance_texture);
            frame_graph.MarkNoCull(ddgi_irradiance_id);
            frame_graph.AddPass("Update DDGI Probes",
                { { ddgi_irradiance_id, RHIResourceState::Undefined, FrameResourceAccess::Type::ReadWrite }, view_constants_read },
                [this, &view](const FrameGraphPassContext& pass_context)
            {
                UpdateDDGIProbe(GetFrameContext(), view, (*pass_context.command_list));
            });
        }

        const FrameResourceId depth_id = targets.depth;
        const FrameResourceId color_id[2] = { targets.color[0], targets.color[1] };
        const FrameResourceId scene_color_id = color_id[0];
        const FrameResourceId back_buffer_id = frame_graph.Import(*back_buffer_binding.resource);
        FrameResourceId shadow_atlas_id = invalid_frame_resource;


        frame_graph.AddPass("Clear View Targets",
            { { scene_color_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::Write },
              { depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::Write } },
            [this, &targets, viewport, scissor](const FrameGraphPassContext& pass_context)
        {
            pass_context.command_list->ClearRenderTarget({ pass_context.GetResource(targets.color[0]), targets.color_rtv[0] }, clear_color);
            pass_context.command_list->ClearDepthStencil({ pass_context.GetResource(targets.depth), targets.depth_dsv }, 0.0f, 0u);
            pass_context.command_list->SetViewport(viewport);
            pass_context.command_list->SetScissor(scissor);
        });

        if (gpu_scene.shader_environment.sky_type != SHADER_SKY_TYPE_NONE)
        {
            frame_graph.AddPass("Sky Pass", { { scene_color_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::Write }, view_constants_read },
                [this, &view, &targets, viewport, scissor, shader_frame_binding](const FrameGraphPassContext& pass_context)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sky Pass", (*pass_context.command_list));
                RHICommandList* command_list = pass_context.command_list;
                const RHISubresourceBinding shader_view_binding = { view.view_constants.buffer.get(), view.view_constants.cbv };
                command_list->SetViewport(viewport);
                command_list->SetScissor(scissor);
                command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[0]), targets.color_rtv[0] } }, nullptr);
                GraphicsPipelineHash sky_pipeline_hash = {};
                sky_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::SkyPass);
                sky_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                sky_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
                sky_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                sky_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                RHIPipeline* sky_pipeline = shader_library.GetPipeline(sky_pipeline_hash);
                if (!sky_pipeline)
                {
                    return;
                }
                command_list->SetGraphicsPipeline(*sky_pipeline);
                command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                command_list->Draw(3, 1, 0, 0);
            });
        }

        if (view.shadow_resources.atlas != invalid_frame_resource && view.shadow_resources.atlas_dsv.IsValid() && !view.shadow_resources.render_shadow_slices.empty())
        {
            shadow_atlas_id = view.shadow_resources.atlas;
            frame_graph.AddPass("Shadow Pass", { { shadow_atlas_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::Write }, view_constants_write, sort_buffer_read },
                [this, &view, &frame_context](const FrameGraphPassContext& pass_context)
            {
                auto cpu_range = profiler::ScopedRangeCPU("Shadow Pass");
                auto gpu_range = profiler::ScopedRangeGPU("Shadow Pass", (*pass_context.command_list));
                RHICommandList* command_list = pass_context.command_list;

                RHIResource* view_constants_resource = view.view_constants.buffer.get();
                const RHISubresourceBinding shader_view_binding = { view_constants_resource, view.view_constants.cbv };

                RHISubresourceBinding shadow_map_atlas_binding = {};
                shadow_map_atlas_binding.resource = pass_context.GetResource(view.shadow_resources.atlas);
                shadow_map_atlas_binding.subresource = view.shadow_resources.atlas_dsv;
                if (!shadow_map_atlas_binding.resource || !view_constants_resource)
                {
                    return;
                }

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

                    if (!UpdateDefaultBuffer(frame_context, *view_constants_resource, &shadow_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer, 0, *command_list))
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

                if (!UpdateDefaultBuffer(frame_context, *view_constants_resource, &shader_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer, 0, *command_list))
                {
                    return;
                }

                command_list->EndEvent();
            });
        }

        View::OcclusionResources& occlusion = view.occlusion_resources;
        const bool occlusion_enabled = view.options.enable_occlusion_culling || r_occlusion_enabled.GetInt() != 0;
        occlusion.active = occlusion_enabled;

        if (occlusion_enabled && !view.freeze_culling && occlusion.readback_buffers[current_frame_slot])
        {
            const Vector<View::OcclusionResources::RenderableKey>& resolved_keys = occlusion.issued_keys[current_frame_slot];
            const uint64* results = static_cast<const uint64*>(occlusion.readback_buffers[current_frame_slot]->GetMappedData());
            if (results)
            {
                for (auto entry = occlusion.visibility.begin(); entry != occlusion.visibility.end(); )
                {
                    entry->second.history <<= 1;
                    entry->second.queried <<= 1;
                    entry = entry->second.IsDead() ? occlusion.visibility.erase(entry) : std::next(entry);
                }

                for (Size i = 0; i < resolved_keys.size(); ++i)
                {
                    View::OcclusionResources::VisibilityEntry& entry = occlusion.visibility[resolved_keys[i]];
                    entry.queried |= 1u;
                    if (results[i] > 0)
                    {
                        entry.history |= 1u;
                    }
                }
            }
        }

        if (occlusion_enabled)
        {
            const uint32 required_queries = static_cast<uint32>(gpu_scene.opaque_renderables.size());
            const uint32 current_capacity = occlusion.query_heap ? occlusion.query_heap->GetDesc().query_count : 0;
            if (required_queries > current_capacity)
            {
                const uint32 new_capacity = static_cast<uint32>(math::Align(static_cast<Size>(required_queries) * 2, static_cast<Size>(1024)));

                RHIQueryHeapDesc query_desc = {};
                query_desc.type = RHIQueryType::BinaryOcclusion; // for early termination
                query_desc.query_count = new_capacity;
                std::unique_ptr<RHIQueryHeap> new_heap = device->CreateQueryHeap(query_desc);

                RHIBufferDesc readback_desc = {};
                readback_desc.usage = RHIResourceUsage::Readback;
                readback_desc.size = static_cast<Size>(new_capacity) * sizeof(uint64);

                std::array<std::unique_ptr<RHIResource>, max_frames_in_flight> new_readback_buffers = {};
                bool readback_buffers_created = true;
                for (uint32 i = 0; i < max_frames_in_flight; ++i)
                {
                    new_readback_buffers[i] = device->CreateBuffer(readback_desc);
                    if (!new_readback_buffers[i])
                    {
                        readback_buffers_created = false;
                        break;
                    }
                    new_readback_buffers[i]->SetName("Occlusion Query Readback Buffer " + std::to_string(i));
                }

                if (new_heap && readback_buffers_created)
                {
                    new_heap->SetName("Occlusion Query Heap");
                    if (occlusion.query_heap)
                    {
                        frame_context.RemoveResourceDeferred(std::move(occlusion.query_heap));
                    }
                    for (auto& buffer : occlusion.readback_buffers)
                    {
                        if (buffer)
                        {
                            frame_context.RemoveResourceDeferred(std::move(buffer));
                        }
                    }
                    for (auto& keys : occlusion.issued_keys)
                    {
                        keys.clear();
                    }
                    occlusion.visibility.clear();
                    occlusion.query_heap = std::move(new_heap);
                    occlusion.readback_buffers = std::move(new_readback_buffers);
                    wonlog("Occlusion culling: query capacity %u", new_capacity);
                }
                else
                {
                    wonlog_warning("Occlusion culling: failed to create query resources");
                }
            }
        }
        else if (occlusion.query_heap)
        {
            frame_context.RemoveResourceDeferred(std::move(occlusion.query_heap));
            for (auto& buffer : occlusion.readback_buffers)
            {
                if (buffer)
                {
                    frame_context.RemoveResourceDeferred(std::move(buffer));
                }
            }
            for (auto& keys : occlusion.issued_keys)
            {
                keys.clear();
            }
            occlusion.visibility.clear();
        }

        // depth only prepass
        if ((view.show_flags & Show_Opaque) != 0)
        {
            frame_graph.AddPass("Depth Only Prepass", { { depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::ReadWrite }, view_constants_read, sort_buffer_read },
                [this, &view, &targets, &frame_context, viewport, scissor](const FrameGraphPassContext& pass_context)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Depth Only Prepass", (*pass_context.command_list));
                auto cpu_range = profiler::ScopedRangeCPU("Depth Only Prepass");
                const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_dsv };
                pass_context.command_list->SetViewport(viewport);
                pass_context.command_list->SetScissor(scissor);
                pass_context.command_list->SetRenderTargets({}, &depth_binding);
                DrawScene(frame_context, view, RenderPassType::DepthPrepass, DrawScene_Opaque, (*pass_context.command_list));
            });
        }

        // light culling for ForwardPlus
        FrameResourceId cluster_count_id = invalid_frame_resource;
        FrameResourceId cluster_offset_id = invalid_frame_resource;
        FrameResourceId cluster_index_id = invalid_frame_resource;
        if (view.render_path_type == RenderPathType::ForwardPlus
            && view.light_resources.cluster_light_count_buffer != invalid_frame_resource
            && view.light_resources.cluster_light_offset_buffer != invalid_frame_resource
            && view.light_resources.cluster_light_index_buffer != invalid_frame_resource)
        {
            RHIPipeline* light_cull_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSLightCull));
            if (light_cull_pipeline)
            {
                cluster_count_id = view.light_resources.cluster_light_count_buffer;
                cluster_offset_id = view.light_resources.cluster_light_offset_buffer;
                cluster_index_id = view.light_resources.cluster_light_index_buffer;
                frame_graph.AddPass("Cull Lights",
                    { { cluster_count_id, RHIResourceState::ShaderWrite, FrameResourceAccess::Type::Write }, view_constants_read,
                      { cluster_offset_id, RHIResourceState::ShaderWrite, FrameResourceAccess::Type::Write },
                      { cluster_index_id, RHIResourceState::ShaderWrite, FrameResourceAccess::Type::Write } },
                    [this, &view, &gpu_scene, light_cull_pipeline](const FrameGraphPassContext& pass_context)
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Cull Lights", (*pass_context.command_list));
                    RHICommandList* command_list = pass_context.command_list;
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

                    command_list->UAVBarrier(*pass_context.GetResource(view.light_resources.cluster_light_count_buffer));
                    command_list->UAVBarrier(*pass_context.GetResource(view.light_resources.cluster_light_offset_buffer));
                    command_list->UAVBarrier(*pass_context.GetResource(view.light_resources.cluster_light_index_buffer));
                });
            }
        }

        // main pass
        uint32 main_pass_flags = 0;
        if ((view.show_flags & Show_Opaque) != 0)
        {
            main_pass_flags |= DrawScene_Opaque;
        }
        if ((view.show_flags & Show_Transparent) != 0)
        {
            main_pass_flags |= DrawScene_Transparent;
        }
        if (main_pass_flags != 0)
        {
            Vector<FrameResourceAccess> main_pass_accesses = {
                view_constants_read,
                sort_buffer_read,
                { scene_color_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite },
				{ depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::ReadWrite }, // depth buffer is written in the main pass for transparent or masked objects
            };
            if (shadow_atlas_id != invalid_frame_resource)
            {
                main_pass_accesses.push_back({ shadow_atlas_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
            }
            if (cluster_count_id != invalid_frame_resource)
            {
                main_pass_accesses.push_back({ cluster_count_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                main_pass_accesses.push_back({ cluster_offset_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                main_pass_accesses.push_back({ cluster_index_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
            }
            if (view.shadow_resources.cascade_buffer != invalid_frame_resource)
            {
                main_pass_accesses.push_back({ view.shadow_resources.cascade_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
            }
            if (view.shadow_resources.light_slice_buffer != invalid_frame_resource)
            {
                main_pass_accesses.push_back({ view.shadow_resources.light_slice_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
            }
            if (view.light_resources.forward_index_buffer != invalid_frame_resource)
            {
                main_pass_accesses.push_back({ view.light_resources.forward_index_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
            }

            frame_graph.AddPass("Main Pass", std::move(main_pass_accesses),
                [this, &view, &targets, &frame_context, viewport, scissor, main_pass_flags](const FrameGraphPassContext& pass_context)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Main Pass", (*pass_context.command_list));
                auto cpu_range = profiler::ScopedRangeCPU("Main Pass");

                const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_dsv };
                pass_context.command_list->SetViewport(viewport);
                pass_context.command_list->SetScissor(scissor);
                pass_context.command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[0]), targets.color_rtv[0] } }, &depth_binding);
                DrawScene(frame_context, view, RenderPassType::MainPass, main_pass_flags, (*pass_context.command_list));
            });
        }

        if (occlusion_enabled && occlusion.query_heap && occlusion.readback_buffers[current_frame_slot]
            && (view.show_flags & Show_Opaque) != 0 && !view.occlusion_query_indices.empty())
        {
            // occlusion query should be performed after main pass becuase depth buffer might be updated on main pass(transparent, masked ..)
            float3 camera_eye = {};
            float camera_near = 0.0f;
            if (const ecs::CameraComponent* occlusion_camera = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity))
            {
                camera_eye = occlusion_camera->eye;
                camera_near = occlusion_camera->near_plane;
            }

            const FrameResourceId occlusion_readback_id = frame_graph.Import(*occlusion.readback_buffers[current_frame_slot]);
            frame_graph.MarkNoCull(occlusion_readback_id);
            const uint32 occlusion_frame_slot = current_frame_slot;

            frame_graph.AddPass("Occlusion Query",
                { { depth_id, RHIResourceState::DepthRead, FrameResourceAccess::Type::Read },
                  { occlusion_readback_id, RHIResourceState::CopyDest, FrameResourceAccess::Type::Write },
                  view_constants_read },
                [this, &view, &targets, viewport, scissor, camera_eye, camera_near, occlusion_frame_slot](const FrameGraphPassContext& pass_context)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Occlusion Query", (*pass_context.command_list));
                auto cpu_range = profiler::ScopedRangeCPU("Occlusion Query");

                GraphicsPipelineHash occlusion_hash = {};
                occlusion_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::OcclusionQueryPass);
                occlusion_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                occlusion_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
                occlusion_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                occlusion_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                RHIPipeline* occlusion_pipeline = shader_library.GetPipeline(occlusion_hash);
                if (!occlusion_pipeline)
                {
                    return;
                }

                View::OcclusionResources& occlusion_resources = view.occlusion_resources;
                Vector<View::OcclusionResources::RenderableKey>& issued_keys = occlusion_resources.issued_keys[occlusion_frame_slot];
                issued_keys.clear();

                const GPUScene& gpu_scene = view.scene->GetGPUScene();
                const uint32 query_capacity = occlusion_resources.query_heap->GetDesc().query_count;
                RHICommandList& command_list = *pass_context.command_list;

                RHISubresourceBinding frame_binding = {};
                frame_binding.resource = shader_frame_buffer.get();
                frame_binding.subresource = shader_frame_buffer_cbv;

                RHISubresourceBinding view_binding = {};
                view_binding.resource = view.view_constants.buffer.get();
                view_binding.subresource = view.view_constants.cbv;

                const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_readonly_dsv };
                command_list.SetViewport(viewport);
                command_list.SetScissor(scissor);
                command_list.SetRenderTargets({}, &depth_binding);
                command_list.SetGraphicsPipeline(*occlusion_pipeline);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, view_binding);
                command_list.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                command_list.ResetQuery(*occlusion_resources.query_heap, 0, query_capacity);

                for (uint32 renderable_index : view.occlusion_query_indices)
                {
                    if (static_cast<uint32>(issued_keys.size()) >= query_capacity)
                    {
                        break;
                    }

                    const Renderable& renderable = gpu_scene.opaque_renderables[renderable_index];
                    if (!renderable.aabb.IsValid())
                    {
                        continue;
                    }

                    const float3 closest_point = {
                        (std::max)(renderable.aabb.min.x, (std::min)(camera_eye.x, renderable.aabb.max.x)),
                        (std::max)(renderable.aabb.min.y, (std::min)(camera_eye.y, renderable.aabb.max.y)),
                        (std::max)(renderable.aabb.min.z, (std::min)(camera_eye.z, renderable.aabb.max.z))
                    };
                    const float distance_squared = math::DistanceSquared(closest_point, camera_eye);
                    if (distance_squared <= camera_near * camera_near)
                    {
                        continue;
                    }

                    const float bounds_expand = std::sqrt(distance_squared) * r_occlusion_bounds_expand.GetFloat();

                    OcclusionPushConstants occlusion_push = {};
                    occlusion_push.Init();
                    occlusion_push.aabb_min = {
                        renderable.aabb.min.x - bounds_expand,
                        renderable.aabb.min.y - bounds_expand,
                        renderable.aabb.min.z - bounds_expand
                    };
                    occlusion_push.aabb_max = {
                        renderable.aabb.max.x + bounds_expand,
                        renderable.aabb.max.y + bounds_expand,
                        renderable.aabb.max.z + bounds_expand
                    };

                    const uint32 query_index = static_cast<uint32>(issued_keys.size());
                    command_list.PushConstants(RHIShaderStage::Vertex, &occlusion_push, sizeof(OcclusionPushConstants), 0);
                    command_list.BeginQuery(*occlusion_resources.query_heap, query_index);
                    command_list.Draw(36, 1, 0, 0);
                    command_list.EndQuery(*occlusion_resources.query_heap, query_index);

                    issued_keys.push_back({ renderable.entity, renderable.push_constants.geometry_index });
                }

                if (!issued_keys.empty())
                {
                    command_list.ResolveQuery(*occlusion_resources.query_heap, 0, static_cast<uint32>(issued_keys.size()),
                        *occlusion_resources.readback_buffers[occlusion_frame_slot], 0);
                }
            });
        }

        // decal pass: project decal volumes onto the scene depth, blending into the HDR color target.
        if ((view.show_flags & Show_Decals) != 0 && !gpu_scene.shader_decals.empty() && gpu_scene.decal_buffer.srv.IsValid() && view.render_targets.depth_srv.IsValid())
        {
            frame_graph.AddPass("Decal Pass",
                { { scene_color_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite }, { depth_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read }, view_constants_read, sort_buffer_read },
                [this, &view, &targets, &frame_context, viewport, scissor](const FrameGraphPassContext& pass_context)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Decal Pass", (*pass_context.command_list));
                RHICommandList* command_list = pass_context.command_list;

                command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[0]), targets.color_rtv[0] } }, nullptr);
                command_list->SetViewport(viewport);
                command_list->SetScissor(scissor);
                DrawScene(frame_context, view, RenderPassType::DecalPass, DrawScene_Decal, *command_list);
            });
        }

        const bool water_pass_active = (view.show_flags & Show_Water) != 0
            && !gpu_scene.water.shader_zones.empty()
            && !view.water_resources.tiles.empty()
            && gpu_scene.water.body_buffer.srv.IsValid()
            && gpu_scene.water.zone_buffer.srv.IsValid()
            && view.render_targets.depth_srv.IsValid()
            && view.render_targets.depth_readonly_dsv.IsValid();

        const bool needs_scene_color_snapshot = water_pass_active;

        targets.scene_color_snapshot = invalid_frame_resource;
        targets.scene_color_snapshot_srv = {};
        if (needs_scene_color_snapshot)
        {
            RHITextureDesc snapshot_desc = {};
            snapshot_desc.width = targets.width;
            snapshot_desc.height = targets.height;
            snapshot_desc.depth = 1;
            snapshot_desc.mip_levels = 1;
            snapshot_desc.array_layers = 1;
            snapshot_desc.sample_count = 1;
            snapshot_desc.format = HDR_COLOR_BUFFER_FORMAT;
            snapshot_desc.usage = RHIResourceUsage::Default;
            snapshot_desc.bind_flags = RHIBindFlags::ShaderResource;
            targets.scene_color_snapshot = frame_graph.CreateTexture(view.viewer_index, "Scene Color Snapshot", snapshot_desc);

            if (targets.scene_color_snapshot != invalid_frame_resource)
            {
                RHISubresourceDesc snapshot_srv_desc = {};
                snapshot_srv_desc.type = RHISubresourceType::ShaderResource;
                snapshot_srv_desc.format = snapshot_desc.format;
                targets.scene_color_snapshot_srv = frame_graph.CreateSubresource(targets.scene_color_snapshot, snapshot_srv_desc);

                frame_graph.AddPass("Copy Scene Color",
                    { { scene_color_id, RHIResourceState::CopySource, FrameResourceAccess::Type::Read },
                      { targets.scene_color_snapshot, RHIResourceState::CopyDest, FrameResourceAccess::Type::Write } },
                    [&targets](const FrameGraphPassContext& pass_context)
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Copy Scene Color", (*pass_context.command_list));
                    pass_context.command_list->CopyResource(*pass_context.GetResource(targets.scene_color_snapshot),
                        *pass_context.GetResource(targets.color[0]));
                });
            }
        }

        if (water_pass_active && targets.scene_color_snapshot_srv.IsValid())
        {
            GraphicsPipelineHash water_pipeline_hash = {};
            water_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::WaterPass);
            water_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            water_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            water_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(view.view_mode == ViewMode::Wireframe ? RHIFillMode::Wireframe : RHIFillMode::Solid);
            water_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
            water_pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(resource::MaterialBlendMode::Opaque);
            water_pipeline_hash.storage.bits.clustered = view.render_path_type == RenderPathType::ForwardPlus ? 1 : 0;

            if (RHIPipeline* water_pipeline = shader_library.GetPipeline(water_pipeline_hash))
            {
                Vector<FrameResourceAccess> water_pass_accesses = {
                    view_constants_read,
                    { scene_color_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite },
                    { depth_id, RHIResourceState::DepthRead, FrameResourceAccess::Type::Read },
                    { targets.scene_color_snapshot, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read },
                };
                if (shadow_atlas_id != invalid_frame_resource)
                {
                    water_pass_accesses.push_back({ shadow_atlas_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                }
                if (cluster_count_id != invalid_frame_resource)
                {
                    water_pass_accesses.push_back({ cluster_count_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                    water_pass_accesses.push_back({ cluster_offset_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                    water_pass_accesses.push_back({ cluster_index_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                }
                if (view.shadow_resources.cascade_buffer != invalid_frame_resource)
                {
                    water_pass_accesses.push_back({ view.shadow_resources.cascade_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                }
                if (view.shadow_resources.light_slice_buffer != invalid_frame_resource)
                {
                    water_pass_accesses.push_back({ view.shadow_resources.light_slice_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                }
                if (view.light_resources.forward_index_buffer != invalid_frame_resource)
                {
                    water_pass_accesses.push_back({ view.light_resources.forward_index_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                }

                const uint32 water_body_buffer_descriptor = static_cast<uint32>(gpu_scene.water.body_buffer.srv.descriptor_index);
                const uint32 water_zone_buffer_descriptor = static_cast<uint32>(gpu_scene.water.zone_buffer.srv.descriptor_index);

                GraphicsPipelineHash water_info_pipeline_hash = {};
                water_info_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::WaterInfoPass);
                water_info_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                water_info_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
                water_info_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                water_info_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                water_info_pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(resource::MaterialBlendMode::Opaque);
                RHIPipeline* water_info_pipeline = shader_library.GetPipeline(water_info_pipeline_hash);

                Vector<uint32> zone_info_descriptors(gpu_scene.water.shader_zones.size(), 0u);
                if (water_info_pipeline)
                {
                    for (uint32 zone_index = 0; zone_index < static_cast<uint32>(gpu_scene.water.shader_zones.size()); ++zone_index)
                    {
                        const ShaderWaterZone& zone = gpu_scene.water.shader_zones[zone_index];
                        const uint32 info_resolution = (std::max)(64u, zone.info_resolution);

                        RHITextureDesc info_desc = {};
                        info_desc.width = info_resolution;
                        info_desc.height = info_resolution;
                        info_desc.depth = 1;
                        info_desc.mip_levels = 1;
                        info_desc.array_layers = 1;
                        info_desc.sample_count = 1;
						info_desc.format = RHIFormat::R32G32Float; // R : water height, G : water body index(-1 for no body)
                        info_desc.usage = RHIResourceUsage::Default;
                        info_desc.bind_flags = RHIBindFlags::RenderTarget | RHIBindFlags::ShaderResource;
                        info_desc.clear_color[0] = 0.0f;
                        info_desc.clear_color[1] = WATER_INFO_NO_BODY;
                        info_desc.clear_color[2] = 0.0f;
                        info_desc.clear_color[3] = 0.0f;
                        const FrameResourceId info_id = frame_graph.CreateTexture(view.viewer_index, "Water Info Texture", info_desc);
                        if (info_id == invalid_frame_resource)
                        {
                            continue;
                        }

                        RHISubresourceDesc info_rtv_desc = {};
                        info_rtv_desc.type = RHISubresourceType::RenderTarget;
                        info_rtv_desc.format = info_desc.format;
                        const RHISubresourceHandle info_rtv = frame_graph.CreateSubresource(info_id, info_rtv_desc);

                        RHISubresourceDesc info_srv_desc = {};
                        info_srv_desc.type = RHISubresourceType::ShaderResource;
                        info_srv_desc.format = info_desc.format;
                        const RHISubresourceHandle info_srv = frame_graph.CreateSubresource(info_id, info_srv_desc);
                        zone_info_descriptors[zone_index] = static_cast<uint32>(info_srv.descriptor_index);
                        water_pass_accesses.push_back({ info_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });

                        RHITextureDesc info_depth_desc = info_desc;
                        info_depth_desc.format = DEPTH_BUFFER_FORMAT;
                        info_depth_desc.bind_flags = RHIBindFlags::DepthStencil;
                        const FrameResourceId info_depth_id = frame_graph.CreateTexture(view.viewer_index, "Water Info Depth", info_depth_desc);
                        if (info_depth_id == invalid_frame_resource)
                        {
                            continue;
                        }

                        RHISubresourceDesc info_dsv_desc = {};
                        info_dsv_desc.type = RHISubresourceType::DepthStencil;
                        info_dsv_desc.format = info_depth_desc.format;
                        const RHISubresourceHandle info_dsv = frame_graph.CreateSubresource(info_depth_id, info_dsv_desc);

                        const uint32 first_body = zone.first_body;
                        const uint32 body_count = zone.body_count;
                        const float max_vertex_spacing = (std::max)(zone.extent.x, zone.extent.y) / static_cast<float>(zone.tile_resolution); // each vertex is dilated in first pass
                        frame_graph.AddPass("Water Info Pass",
                            { { info_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::Write },
                              { info_depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::Write } },
                            [water_info_pipeline, info_id, info_rtv, info_depth_id, info_dsv, info_resolution, zone_index, first_body, body_count, max_vertex_spacing,
                             water_body_buffer_descriptor, water_zone_buffer_descriptor](const FrameGraphPassContext& pass_context)
                        {
                            
                            auto gpu_range = profiler::ScopedRangeGPU("Water Info Pass", (*pass_context.command_list));
                            RHICommandList* command_list = pass_context.command_list;
                            const RHISubresourceBinding info_binding = { pass_context.GetResource(info_id), info_rtv };
                            const RHISubresourceBinding info_depth_binding = { pass_context.GetResource(info_depth_id), info_dsv };

                            const RHIClearColor info_clear = { 0.0f, WATER_INFO_NO_BODY, 0.0f, 0.0f };
                            command_list->ClearRenderTarget(info_binding, info_clear);
                            command_list->ClearDepthStencil(info_depth_binding, 0.0f, 0u);
                            command_list->SetRenderTargets({ info_binding }, &info_depth_binding);
                            command_list->SetViewport({ 0.0f, 0.0f, static_cast<float>(info_resolution), static_cast<float>(info_resolution), 0.0f, 1.0f });
                            command_list->SetScissor({ 0, 0, static_cast<int32>(info_resolution), static_cast<int32>(info_resolution) });
                            command_list->SetGraphicsPipeline(*water_info_pipeline);
                            command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

                            WaterInfoPushConstants info_push = {};
                            info_push.Init();
                            info_push.zone_buffer_descriptor = water_zone_buffer_descriptor;
                            info_push.body_buffer_descriptor = water_body_buffer_descriptor;
                            info_push.zone_index = zone_index;
                            info_push.first_body = first_body;

							// first pass for "expanded" height, each vertex is dilated
							info_push.max_vertex_spacing = max_vertex_spacing; // biggest vertex spacing for the body quads
                            info_push.writes_body_index = 0u;
                            command_list->PushConstants(RHIShaderStage::Vertex, &info_push, sizeof(WaterInfoPushConstants), 0);
                            command_list->Draw(6, body_count, 0, 0);

							// second pass for body index
                            info_push.max_vertex_spacing = 0.0f;
                            info_push.writes_body_index = 1u;
                            command_list->PushConstants(RHIShaderStage::Vertex, &info_push, sizeof(WaterInfoPushConstants), 0);
                            command_list->Draw(6, body_count, 0, 0);
                        });
                    }
                }

                if (view.water_resources.tile_buffer != invalid_frame_resource)
                {
                    water_pass_accesses.push_back({ view.water_resources.tile_buffer, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read });
                }

                frame_graph.AddPass("Water Pass", std::move(water_pass_accesses),
                    [&view, &targets, &gpu_scene, viewport, scissor, water_pipeline, shader_frame_binding,
                     water_body_buffer_descriptor, water_zone_buffer_descriptor, zone_info_descriptors = std::move(zone_info_descriptors)](const FrameGraphPassContext& pass_context)
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Water Pass", (*pass_context.command_list));
                    RHICommandList* command_list = pass_context.command_list;
                    const RHISubresourceBinding shader_view_binding = { view.view_constants.buffer.get(), view.view_constants.cbv };
                    const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_readonly_dsv };

                    command_list->SetViewport(viewport);
                    command_list->SetScissor(scissor);
                    command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[0]), targets.color_rtv[0] } }, &depth_binding);
                    command_list->SetGraphicsPipeline(*water_pipeline);
                    command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                    command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

                    WaterPushConstants water_push = {};
                    water_push.Init();
                    water_push.depth_descriptor = static_cast<uint32>(view.render_targets.depth_srv.descriptor_index);
                    water_push.scene_color_descriptor = static_cast<uint32>(view.render_targets.scene_color_snapshot_srv.descriptor_index);
                    water_push.body_buffer_descriptor = water_body_buffer_descriptor;
                    water_push.zone_buffer_descriptor = water_zone_buffer_descriptor;
                    water_push.tile_buffer_descriptor = static_cast<uint32>(view.water_resources.tile_srv.descriptor_index);
                    for (uint32 zone_index = 0; zone_index < static_cast<uint32>(gpu_scene.water.shader_zones.size()); ++zone_index)
                    {
                        const View::WaterResources::TileRange& tile_range = view.water_resources.zone_tile_ranges[zone_index];
                        if (zone_info_descriptors[zone_index] == 0 || tile_range.tile_count == 0)
                        {
                            continue;
                        }
                        const ShaderWaterZone& zone = gpu_scene.water.shader_zones[zone_index];
                        const uint32 tile_resolution = zone.tile_resolution;
                        water_push.zone_index = zone_index;
                        water_push.info_texture_descriptor = zone_info_descriptors[zone_index];
                        water_push.first_tile = tile_range.first_tile;
                        water_push.tile_resolution = tile_resolution;
                        command_list->PushConstants(RHIShaderStage::Vertex, &water_push, sizeof(WaterPushConstants), 0);
                        command_list->Draw(tile_resolution * tile_resolution * 6, tile_range.tile_count, 0, 0);
                    }
                });
            }
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
            const uint32 width = targets.width;
            const uint32 height = targets.height;

            const ecs::CameraComponent* camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
            const bool auto_exposure_active = camera_component && camera_component->IsAutoExposure();

            RHIPipeline* tonemap_pipeline = shader_library.GetPipeline(ComputePipelineHash(ShaderId::CSTonemap));

            FrameResourceId partial_id = invalid_frame_resource;
            FrameResourceId luminance_id = invalid_frame_resource;
            RHISubresourceHandle luminance_partial_uav = {};
            RHISubresourceHandle luminance_partial_srv = {};
            RHISubresourceHandle luminance_uav = {};
            if (auto_exposure_active)
            {
                RHIBufferDesc luminance_partial_desc = {};
                luminance_partial_desc.size = sizeof(float) * luminance_reduce_group_count;
                luminance_partial_desc.usage = RHIResourceUsage::Default;
                luminance_partial_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                partial_id = frame_graph.CreateBuffer(view.viewer_index, "Auto-Exposure Luminance Partials", luminance_partial_desc);

                RHISubresourceDesc luminance_partial_uav_desc = {};
                luminance_partial_uav_desc.type = RHISubresourceType::UnorderedAccess;
                luminance_partial_uav_desc.buffer_offset = 0;
                luminance_partial_uav_desc.buffer_size = luminance_partial_desc.size;
                luminance_partial_uav_desc.buffer_stride = sizeof(float);
                RHISubresourceDesc luminance_partial_srv_desc = luminance_partial_uav_desc;
                luminance_partial_srv_desc.type = RHISubresourceType::ShaderResource;
                if (partial_id != invalid_frame_resource)
                {
                    luminance_partial_uav = frame_graph.CreateSubresource(partial_id, luminance_partial_uav_desc);
                    luminance_partial_srv = frame_graph.CreateSubresource(partial_id, luminance_partial_srv_desc);
                }

                RHIBufferDesc luminance_buffer_desc = {};
                luminance_buffer_desc.size = sizeof(float);
                luminance_buffer_desc.usage = RHIResourceUsage::Default;
                luminance_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
                luminance_id = frame_graph.CreateBuffer(view.viewer_index, "Auto-Exposure Luminance Buffer", luminance_buffer_desc);

                RHISubresourceDesc luminance_uav_desc = {};
                luminance_uav_desc.type = RHISubresourceType::UnorderedAccess;
                luminance_uav_desc.buffer_offset = 0;
                luminance_uav_desc.buffer_size = sizeof(float);
                luminance_uav_desc.buffer_stride = sizeof(float);
                if (luminance_id != invalid_frame_resource)
                {
                    luminance_uav = frame_graph.CreateSubresource(luminance_id, luminance_uav_desc);
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

            const bool use_post_chain = tonemap_pipeline || (use_fxaa && fxaa_pipeline);

            if (auto_exposure_active && luminance_reduce_pipeline && luminance_resolve_pipeline
                && partial_id != invalid_frame_resource && luminance_id != invalid_frame_resource && exposure.luminance_readback_buffer)
            {
                const FrameResourceId luminance_readback_id = frame_graph.Import(*exposure.luminance_readback_buffer);
                frame_graph.MarkNoCull(luminance_readback_id);

                frame_graph.AddPass("Reduce Luminance",
                    { { color_id[src], RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read }, { partial_id, RHIResourceState::ShaderWrite, FrameResourceAccess::Type::Write } },
                    [&view, &targets, src, partial_id, luminance_reduce_pipeline, luminance_partial_uav](const FrameGraphPassContext& pass_context)
                {
                    RHICommandList* command_list = pass_context.command_list;
                    command_list->SetComputePipeline(*luminance_reduce_pipeline);
                    LuminanceReducePushConstants reduce_push = {};
                    reduce_push.Init();
                    reduce_push.input_descriptor = static_cast<uint32>(targets.color_srv[src].descriptor_index);
                    reduce_push.output_descriptor = static_cast<uint32>(luminance_partial_uav.descriptor_index);
                    reduce_push.viewport_size = uint2(static_cast<uint32>(view.viewport.width), static_cast<uint32>(view.viewport.height));
                    reduce_push.viewport_offset = uint2(static_cast<uint32>(view.viewport.x), static_cast<uint32>(view.viewport.y));
                    command_list->PushConstants(RHIShaderStage::Compute, &reduce_push, sizeof(reduce_push), 0);
					    command_list->Dispatch(luminance_reduce_group_count, 1u, 1u); // reduce to luminance_reduce_group_count groups of 1D data
                    command_list->UAVBarrier(*pass_context.GetResource(partial_id));
                });

                frame_graph.AddPass("Resolve Luminance",
                    { { partial_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read }, { luminance_id, RHIResourceState::ShaderWrite, FrameResourceAccess::Type::Write } },
                    [luminance_resolve_pipeline, luminance_id, luminance_partial_srv, luminance_uav](const FrameGraphPassContext& pass_context)
                {
                    RHICommandList* command_list = pass_context.command_list;
                    command_list->SetComputePipeline(*luminance_resolve_pipeline);
                    LuminanceReducePushConstants resolve_push = {};
                    resolve_push.Init();
                    resolve_push.input_descriptor = static_cast<uint32>(luminance_partial_srv.descriptor_index);
                    resolve_push.output_descriptor = static_cast<uint32>(luminance_uav.descriptor_index);
                    command_list->PushConstants(RHIShaderStage::Compute, &resolve_push, sizeof(resolve_push), 0);
                    command_list->Dispatch(1u, 1u, 1u);
                    command_list->UAVBarrier(*pass_context.GetResource(luminance_id));
                });

                frame_graph.AddPass("Copy Luminance Readback",
                    { { luminance_id, RHIResourceState::CopySource, FrameResourceAccess::Type::Read },
                      { luminance_readback_id, RHIResourceState::CopyDest, FrameResourceAccess::Type::Write } },
                    [&exposure, luminance_id](const FrameGraphPassContext& pass_context)
                {
                    pass_context.command_list->CopyBuffer(*exposure.luminance_readback_buffer, 0, *pass_context.GetResource(luminance_id), 0, sizeof(float));
                });

            }

            if (use_post_chain)
            {
                frame_graph.AddPass("Init Post Color",
                    { { color_id[1], RHIResourceState::RenderTarget, FrameResourceAccess::Type::Write } },
                    [this, &targets](const FrameGraphPassContext& pass_context)
                {
                    pass_context.command_list->ClearRenderTarget({ pass_context.GetResource(targets.color[1]), targets.color_rtv[1] }, clear_color);
                });
            }

            if (tonemap_pipeline)
            {
                const uint32 dst = src ^ 1u;
                frame_graph.AddPass("Tonemap",
                    { { color_id[src], RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read }, { color_id[dst], RHIResourceState::ShaderWrite, FrameResourceAccess::Type::Write }, view_constants_read },
                    [&view, &targets, shader_frame_binding, src, dst, tonemap_pipeline, width, height](const FrameGraphPassContext& pass_context)
                {
                    RHICommandList* command_list = pass_context.command_list;
                    command_list->SetComputePipeline(*tonemap_pipeline);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 1, { view.view_constants.buffer.get(), view.view_constants.cbv });
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
                    command_list->UAVBarrier(*pass_context.GetResource(targets.color[dst]));
                });

                src = dst;
            }

            if (use_fxaa && fxaa_pipeline)
            {
                const uint32 dst = src ^ 1u;
                frame_graph.AddPass("FXAA",
                    { { color_id[src], RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read }, { color_id[dst], RHIResourceState::ShaderWrite, FrameResourceAccess::Type::Write }, view_constants_read },
                    [&view, &targets, shader_frame_binding, src, dst, fxaa_pipeline, width, height](const FrameGraphPassContext& pass_context)
                {
                    RHICommandList* command_list = pass_context.command_list;
                    command_list->SetComputePipeline(*fxaa_pipeline);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Compute, 1, { view.view_constants.buffer.get(), view.view_constants.cbv });
                    FXAAPushConstants fxaa_push = {};
                    fxaa_push.Init();
                    fxaa_push.input_descriptor = static_cast<uint32>(targets.color_srv[src].descriptor_index);
                    fxaa_push.output_descriptor = static_cast<uint32>(targets.color_uav[dst].descriptor_index);
                    fxaa_push.rcp_resolution = float2(1.0f / static_cast<float>(width), 1.0f / static_cast<float>(height));
                    fxaa_push.resolution = uint2(width, height);
                    command_list->PushConstants(RHIShaderStage::Compute, &fxaa_push, sizeof(fxaa_push), 0);
                    command_list->Dispatch((width + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D,
                                           (height + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D, 1u);
                    command_list->UAVBarrier(*pass_context.GetResource(targets.color[dst]));
                });

                src = dst;
            }
        }

        const FrameResourceId view_output_id = color_id[src];

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
                frame_graph.AddPass("Grid Pass",
                    { { view_output_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite }, { depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::ReadWrite }, view_constants_read },
                    [&view, &targets, viewport, scissor, src, grid_pipeline, shader_frame_binding](const FrameGraphPassContext& pass_context)
                {
                    auto gpu_range = profiler::ScopedRangeGPU("Grid Pass", (*pass_context.command_list));
                    RHICommandList* command_list = pass_context.command_list;
                    const RHISubresourceBinding shader_view_binding = { view.view_constants.buffer.get(), view.view_constants.cbv };

                    const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_dsv };
                    command_list->SetViewport(viewport);
                    command_list->SetScissor(scissor);
                    command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[src]), targets.color_rtv[src] } }, &depth_binding);
                    command_list->SetGraphicsPipeline(*grid_pipeline);
                    command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_view_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_view_binding);
                    command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                    command_list->Draw(3, 1, 0, 0);
                });
            }
        }

        // primitive (line/point) pass: depth-tested against the scene
        frame_graph.AddPass("Primitive Pass",
            { { view_output_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite }, { depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::ReadWrite }, view_constants_read, sort_buffer_read },
            [this, &view, &targets, &frame_context, viewport, scissor, src](const FrameGraphPassContext& pass_context)
        {
            auto gpu_range = profiler::ScopedRangeGPU("Primitive Pass", (*pass_context.command_list));
            auto cpu_range = profiler::ScopedRangeCPU("Primitive Pass");

            const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_dsv };
            pass_context.command_list->SetViewport(viewport);
            pass_context.command_list->SetScissor(scissor);
            pass_context.command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[src]), targets.color_rtv[src] } }, &depth_binding);
            DrawScene(frame_context, view, RenderPassType::PrimitivePass, DrawScene_Primitive, (*pass_context.command_list));
        });

        // sprite/text 3d pass
        if ((view.show_flags & Show_Sprites3D) != 0)
        {
            frame_graph.AddPass("Sprite/Text3D Pass",
                { { view_output_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite }, { depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::ReadWrite }, view_constants_read, sort_buffer_read },
                [this, &view, &targets, &frame_context, src](const FrameGraphPassContext& pass_context)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sprite/Text3D Pass", (*pass_context.command_list));
                auto cpu_range = profiler::ScopedRangeCPU("Sprite/Text3D Pass");

                const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_dsv };
                pass_context.command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[src]), targets.color_rtv[src] } }, &depth_binding);
                DrawScene(frame_context, view, RenderPassType::Sprite3DPass, DrawScene_3DSprite, (*pass_context.command_list));
            });
        }

#ifndef WON_SHIPPING
        // drawn after tonemap so debug colors are authored and displayed in LDR
        frame_graph.AddPass("DebugDraw3D Pass",
            { { view_output_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite }, { depth_id, RHIResourceState::DepthWrite, FrameResourceAccess::Type::ReadWrite }, view_constants_read },
            [this, &view, &targets, src](const FrameGraphPassContext& pass_context)
        {
            auto gpu_range = profiler::ScopedRangeGPU("DebugDraw3D Pass", (*pass_context.command_list));

            const RHISubresourceBinding depth_binding = { pass_context.GetResource(targets.depth), targets.depth_dsv };
            pass_context.command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[src]), targets.color_rtv[src] } }, &depth_binding);
            BuildDebug3D(view);
            DrawDebug3D(view, (*pass_context.command_list));
        });
#endif

        // sprite 2d pass
        if ((view.show_flags & Show_Sprites2D) != 0)
        {
            frame_graph.AddPass("Sprite2D Pass", { { view_output_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite }, view_constants_read, sort_buffer_read },
                [this, &view, &targets, &frame_context, src](const FrameGraphPassContext& pass_context)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sprite2D Pass", (*pass_context.command_list));
                auto cpu_range = profiler::ScopedRangeCPU("Sprite2D Pass");

                pass_context.command_list->SetRenderTargets({ { pass_context.GetResource(targets.color[src]), targets.color_rtv[src] } }, nullptr);
                DrawScene(frame_context, view, RenderPassType::Sprite2DPass, DrawScene_2DSprite, (*pass_context.command_list));
            });
        }

        // composite the finished view into the backbuffer at its viewport rect
        if (composite_pipeline)
        {
            frame_graph.AddPass("Composite",
                { { view_output_id, RHIResourceState::ShaderRead, FrameResourceAccess::Type::Read }, { back_buffer_id, RHIResourceState::RenderTarget, FrameResourceAccess::Type::ReadWrite }, view_constants_read },
                [&view, &targets, back_buffer_binding, composite_pipeline, src](const FrameGraphPassContext& pass_context)
            {
            auto gpu_range = profiler::ScopedRangeGPU("Composite", (*pass_context.command_list));
            RHICommandList* command_list = pass_context.command_list;

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
            });
        }

    }

    void RendererInternal::EndFrame()
    {
        upload_completed_component_mask = 0;

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

        frame_graph.Compile();
        frame_graph.Dispatch(GetRenderingWorkContext());

        {
            auto cpu_range = profiler::ScopedRangeCPU("Wait Render Job");
            jobsystem::Wait(GetRenderingWorkContext());
        }
        RHICommandList* final_command_list = frame_context.BeginCommandList(*device);

        profiler::EndFrameGPU(*final_command_list);
        final_command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::Undefined, RHIResourceState::Present);

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
                frame_context.deferred_shared_res_removal.clear();
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

