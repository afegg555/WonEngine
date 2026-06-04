#include "RendererInternal.h"
#include "ShaderInterop_Sprite.h"

#include "Backlog.h"
#include "Profiler.h"
#include "RenderingUtils.h"
#include "Scene.h"
#include "ShaderLibrary.h"
#include "MathUtils.h"

#include "Window.h"
#include "Entity.h"
#include "CameraComponent.h"
#include "RectPacker.h"

#include "ShaderInterop.h"

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

    RendererDebugState RendererInternal::GetDebugState() const
    {
        return debug_state;
    }

    void RendererInternal::SetClearColor(const RHIClearColor& color)
    {
        clear_color = color;
    }

    RHIClearColor RendererInternal::GetClearColor() const
    {
        return clear_color;
    }

    bool RendererInternal::GetCurrentBackBufferBinding(RHISubresourceBinding& out_binding) const
    {
        if (!current_window)
        {
            return false;
        }

        std::shared_ptr<RHISwapchain> swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            return false;
        }

        const uint32 back_buffer_index = swapchain->GetCurrentBackBufferIndex();
        std::shared_ptr<RHIResource> back_buffer = swapchain->GetCurrentBackBuffer();
        if (!back_buffer || back_buffer_index >= back_buffers_rtv.size() || !back_buffers_rtv[back_buffer_index].IsValid())
        {
            return false;
        }

        out_binding.resource = back_buffer.get();
        out_binding.subresource = back_buffers_rtv[back_buffer_index];
        return true;
    }

    bool RendererInternal::GetCurrentDepthBufferBinding(RHISubresourceBinding& out_binding) const
    {
        if (!depth_buffer || !depth_buffer_dsv.IsValid())
        {
            return false;
        }

        out_binding.resource = depth_buffer.get();
        out_binding.subresource = depth_buffer_dsv;
        return true;
    }

    void RendererInternal::SetDebugOptions(const RendererDebugOptions& options)
    {
        debug_options = options;
    }

    bool RendererInternal::UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset, RHICommandList& command_list)
    {
        const RHIResourceDesc& destination_desc = destination_buffer.GetDesc();
        Size upload_alignment = device->GetMinOffsetAlignment(destination_desc.buffer_desc);

        FrameUploadAllocation upload_allocation = {};
        if (!frame_context.AllocateFrameUpload(*device, data_size, upload_alignment, upload_allocation))
        {
            return false;
        }

        std::memcpy(upload_allocation.mapped_data, source_data, data_size);

        command_list.TransitionResource(destination_buffer, RHIResourceState::CopyDest);
        command_list.CopyBuffer(destination_buffer, destination_offset, *upload_allocation.buffer, upload_allocation.buffer_offset, data_size);
        command_list.TransitionResource(destination_buffer, final_state);
        return true;
    }

    void RendererInternal::ReleaseDDGIResources(FrameContext& frame_context)
    {
        frame_context.RemoveResourceDeferred(ddgi_irradiance_texture);
        frame_context.RemoveResourceDeferred(ddgi_irradiance_history_texture);
        frame_context.RemoveResourceDeferred(ddgi_visibility_texture);
        frame_context.RemoveResourceDeferred(ddgi_visibility_history_texture);
        frame_context.RemoveResourceDeferred(ddgi_probe_data_buffer);
        frame_context.RemoveResourceDeferred(ddgi_probe_data_history_buffer);
        frame_context.RemoveResourceDeferred(debug_state.ddgi.probe_data_readback_buffer);
        debug_state.ddgi.probe_data_readback_valid = false;
        ddgi_irradiance_texture_srv = {};
        ddgi_irradiance_texture_uav = {};
        ddgi_irradiance_history_texture_srv = {};
        ddgi_visibility_texture_srv = {};
        ddgi_visibility_texture_uav = {};
        ddgi_visibility_history_texture_srv = {};
        ddgi_probe_data_buffer_srv = {};
        ddgi_probe_data_buffer_uav = {};
        ddgi_probe_data_history_buffer_srv = {};
        ddgi_probe_counts = { 0, 0, 0 };
        ddgi_probe_spacing = { 0.0f, 0.0f, 0.0f };
        ddgi_volume_min = { 0.0f, 0.0f, 0.0f };
        ddgi_max_distance = 0.0f;
        ddgi_probe_update_offset = 0;
        ddgi_history_valid = false;
    }

    bool RendererInternal::CreateDDGIResources(FrameContext& frame_context, const ShaderDDGIVolume& ddgi_volume)
    {
        if ((ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) == 0)
        {
            ReleaseDDGIResources(frame_context);
            return true;
        }

        const bool recreate_ddgi_texture =
            !ddgi_irradiance_texture ||
            !ddgi_irradiance_texture_srv.IsValid() ||
            !ddgi_irradiance_texture_uav.IsValid() ||
            !ddgi_irradiance_history_texture ||
            !ddgi_irradiance_history_texture_srv.IsValid() ||
            !ddgi_visibility_texture ||
            !ddgi_visibility_texture_srv.IsValid() ||
            !ddgi_visibility_texture_uav.IsValid() ||
            !ddgi_visibility_history_texture ||
            !ddgi_visibility_history_texture_srv.IsValid() ||
            !ddgi_probe_data_buffer ||
            !ddgi_probe_data_buffer_srv.IsValid() ||
            !ddgi_probe_data_buffer_uav.IsValid() ||
            !ddgi_probe_data_history_buffer ||
            !ddgi_probe_data_history_buffer_srv.IsValid() ||
            ddgi_probe_counts.x != ddgi_volume.probe_counts.x ||
            ddgi_probe_counts.y != ddgi_volume.probe_counts.y ||
            ddgi_probe_counts.z != ddgi_volume.probe_counts.z;

        if (!recreate_ddgi_texture)
        {
            if (debug_options.ddgi_debug_enable && !debug_state.ddgi.probe_data_readback_buffer && ddgi_probe_data_buffer)
            {
                RHIBufferDesc probe_data_readback_buffer_desc = {};
                probe_data_readback_buffer_desc.size = ddgi_probe_data_buffer->GetDesc().buffer_desc.size;
                probe_data_readback_buffer_desc.usage = RHIResourceUsage::Readback;
                debug_state.ddgi.probe_data_readback_buffer = device->CreateBuffer(probe_data_readback_buffer_desc);
                if (!debug_state.ddgi.probe_data_readback_buffer)
                {
                    backlog::Post("failed to create ddgi debug probe data readback buffer", backlog::LogLevel::Error);
                    return false;
                }
                debug_state.ddgi.probe_data_readback_buffer->SetName("DDGI Debug Probe Data Readback Buffer");
                debug_state.ddgi.probe_data_readback_valid = false;
            }
            else if (!debug_options.ddgi_debug_enable && debug_state.ddgi.probe_data_readback_buffer)
            {
                frame_context.RemoveResourceDeferred(debug_state.ddgi.probe_data_readback_buffer);
                debug_state.ddgi.probe_data_readback_valid = false;
            }

            const bool reset_ddgi_history =
                ddgi_probe_spacing.x != ddgi_volume.probe_spacing.x ||
                ddgi_probe_spacing.y != ddgi_volume.probe_spacing.y ||
                ddgi_probe_spacing.z != ddgi_volume.probe_spacing.z ||
                ddgi_volume_min.x != ddgi_volume.volume_min.x ||
                ddgi_volume_min.y != ddgi_volume.volume_min.y ||
                ddgi_volume_min.z != ddgi_volume.volume_min.z ||
                ddgi_max_distance != ddgi_volume.max_distance;
            if (reset_ddgi_history)
            {
                ddgi_probe_spacing = ddgi_volume.probe_spacing;
                ddgi_volume_min = ddgi_volume.volume_min;
                ddgi_max_distance = ddgi_volume.max_distance;
                ddgi_probe_update_offset = 0;
                ddgi_history_valid = false;
            }
            return true;
        }

        ReleaseDDGIResources(frame_context);

        RHITextureDesc ddgi_irradiance_texture_desc = {};
        ddgi_irradiance_texture_desc.width = (std::max)(ddgi_volume.probe_counts.x, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2);
        ddgi_irradiance_texture_desc.height = (std::max)(ddgi_volume.probe_counts.y, 1u) * (DDGI_IRRADIANCE_RESOLUTION + 2);
        ddgi_irradiance_texture_desc.depth = 1;
        ddgi_irradiance_texture_desc.mip_levels = 1;
        ddgi_irradiance_texture_desc.array_layers = (std::max)(ddgi_volume.probe_counts.z, 1u);
        ddgi_irradiance_texture_desc.sample_count = 1;
        ddgi_irradiance_texture_desc.format = RHIFormat::R16G16B16A16Float;
        ddgi_irradiance_texture_desc.usage = RHIResourceUsage::Default;
        ddgi_irradiance_texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
        ddgi_irradiance_texture = device->CreateTexture(ddgi_irradiance_texture_desc);
        if (!ddgi_irradiance_texture)
        {
            backlog::Post("failed to create ddgi irradiance texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi_irradiance_texture->SetName("DDGI Irradiance Texture");

        RHISubresourceDesc ddgi_irradiance_srv_desc = {};
        ddgi_irradiance_srv_desc.type = RHISubresourceType::ShaderResource;
        ddgi_irradiance_srv_desc.format = ddgi_irradiance_texture_desc.format;
        ddgi_irradiance_srv_desc.first_slice = 0;
        ddgi_irradiance_srv_desc.slice_count = ddgi_irradiance_texture_desc.array_layers;
        ddgi_irradiance_srv_desc.first_mip = 0;
        ddgi_irradiance_srv_desc.mip_count = 1;
        if (!device->CreateSubresource(*ddgi_irradiance_texture, ddgi_irradiance_srv_desc, &ddgi_irradiance_texture_srv))
        {
            backlog::Post("failed to create ddgi irradiance srv", backlog::LogLevel::Error);
            ddgi_irradiance_texture = nullptr;
            return false;
        }

        RHISubresourceDesc ddgi_irradiance_uav_desc = {};
        ddgi_irradiance_uav_desc.type = RHISubresourceType::UnorderedAccess;
        ddgi_irradiance_uav_desc.format = ddgi_irradiance_texture_desc.format;
        ddgi_irradiance_uav_desc.first_mip = 0;
        ddgi_irradiance_uav_desc.mip_count = 1;
        ddgi_irradiance_uav_desc.first_slice = 0;
        ddgi_irradiance_uav_desc.slice_count = ddgi_irradiance_texture_desc.array_layers;
        if (!device->CreateSubresource(*ddgi_irradiance_texture, ddgi_irradiance_uav_desc, &ddgi_irradiance_texture_uav))
        {
            backlog::Post("failed to create ddgi irradiance uav", backlog::LogLevel::Error);
            ddgi_irradiance_texture = nullptr;
            return false;
        }

        ddgi_irradiance_history_texture = device->CreateTexture(ddgi_irradiance_texture_desc);
        if (!ddgi_irradiance_history_texture)
        {
            backlog::Post("failed to create ddgi irradiance history texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi_irradiance_history_texture->SetName("DDGI Irradiance History Texture");
        if (!device->CreateSubresource(*ddgi_irradiance_history_texture, ddgi_irradiance_srv_desc, &ddgi_irradiance_history_texture_srv))
        {
            backlog::Post("failed to create ddgi irradiance history srv", backlog::LogLevel::Error);
            ddgi_irradiance_history_texture = nullptr;
            return false;
        }

        RHITextureDesc ddgi_visibility_texture_desc = {};
        ddgi_visibility_texture_desc.width = (std::max)(ddgi_volume.probe_counts.x, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2);
        ddgi_visibility_texture_desc.height = (std::max)(ddgi_volume.probe_counts.y, 1u) * (DDGI_VISIBILITY_RESOLUTION + 2);
        ddgi_visibility_texture_desc.depth = 1;
        ddgi_visibility_texture_desc.mip_levels = 1;
        ddgi_visibility_texture_desc.array_layers = (std::max)(ddgi_volume.probe_counts.z, 1u);
        ddgi_visibility_texture_desc.sample_count = 1;
        ddgi_visibility_texture_desc.format = RHIFormat::R16G16B16A16Float;
        ddgi_visibility_texture_desc.usage = RHIResourceUsage::Default;
        ddgi_visibility_texture_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
        ddgi_visibility_texture = device->CreateTexture(ddgi_visibility_texture_desc);
        if (!ddgi_visibility_texture)
        {
            backlog::Post("failed to create ddgi visibility texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi_visibility_texture->SetName("DDGI Visibility Texture");

        RHISubresourceDesc ddgi_visibility_srv_desc = {};
        ddgi_visibility_srv_desc.type = RHISubresourceType::ShaderResource;
        ddgi_visibility_srv_desc.format = ddgi_visibility_texture_desc.format;
        ddgi_visibility_srv_desc.first_slice = 0;
        ddgi_visibility_srv_desc.slice_count = ddgi_visibility_texture_desc.array_layers;
        ddgi_visibility_srv_desc.first_mip = 0;
        ddgi_visibility_srv_desc.mip_count = 1;
        if (!device->CreateSubresource(*ddgi_visibility_texture, ddgi_visibility_srv_desc, &ddgi_visibility_texture_srv))
        {
            backlog::Post("failed to create ddgi visibility srv", backlog::LogLevel::Error);
            ddgi_visibility_texture = nullptr;
            return false;
        }

        RHISubresourceDesc ddgi_visibility_uav_desc = {};
        ddgi_visibility_uav_desc.type = RHISubresourceType::UnorderedAccess;
        ddgi_visibility_uav_desc.format = ddgi_visibility_texture_desc.format;
        ddgi_visibility_uav_desc.first_mip = 0;
        ddgi_visibility_uav_desc.mip_count = 1;
        ddgi_visibility_uav_desc.first_slice = 0;
        ddgi_visibility_uav_desc.slice_count = ddgi_visibility_texture_desc.array_layers;
        if (!device->CreateSubresource(*ddgi_visibility_texture, ddgi_visibility_uav_desc, &ddgi_visibility_texture_uav))
        {
            backlog::Post("failed to create ddgi visibility uav", backlog::LogLevel::Error);
            ddgi_visibility_texture = nullptr;
            return false;
        }

        ddgi_visibility_history_texture = device->CreateTexture(ddgi_visibility_texture_desc);
        if (!ddgi_visibility_history_texture)
        {
            backlog::Post("failed to create ddgi visibility history texture", backlog::LogLevel::Error);
            return false;
        }
        ddgi_visibility_history_texture->SetName("DDGI Visibility History Texture");
        if (!device->CreateSubresource(*ddgi_visibility_history_texture, ddgi_visibility_srv_desc, &ddgi_visibility_history_texture_srv))
        {
            backlog::Post("failed to create ddgi visibility history srv", backlog::LogLevel::Error);
            ddgi_visibility_history_texture = nullptr;
            return false;
        }

        const uint32 total_probe_count = (std::max)(ddgi_volume.total_probe_count, 1u);
        RHIBufferDesc ddgi_probe_data_buffer_desc = {};
        ddgi_probe_data_buffer_desc.size = sizeof(float4) * total_probe_count;
        ddgi_probe_data_buffer_desc.usage = RHIResourceUsage::Default;
        ddgi_probe_data_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
        ddgi_probe_data_buffer = device->CreateBuffer(ddgi_probe_data_buffer_desc);
        if (!ddgi_probe_data_buffer)
        {
            backlog::Post("failed to create ddgi probe data buffer", backlog::LogLevel::Error);
            return false;
        }
        ddgi_probe_data_buffer->SetName("DDGI Probe Data Buffer");

        RHISubresourceDesc ddgi_probe_data_srv_desc = {};
        ddgi_probe_data_srv_desc.type = RHISubresourceType::ShaderResource;
        ddgi_probe_data_srv_desc.buffer_offset = 0;
        ddgi_probe_data_srv_desc.buffer_size = ddgi_probe_data_buffer->GetDesc().buffer_desc.size;
        ddgi_probe_data_srv_desc.buffer_stride = sizeof(float4);
        if (!device->CreateSubresource(*ddgi_probe_data_buffer, ddgi_probe_data_srv_desc, &ddgi_probe_data_buffer_srv))
        {
            backlog::Post("failed to create ddgi probe data srv", backlog::LogLevel::Error);
            ddgi_probe_data_buffer = nullptr;
            return false;
        }

        RHISubresourceDesc ddgi_probe_data_uav_desc = {};
        ddgi_probe_data_uav_desc.type = RHISubresourceType::UnorderedAccess;
        ddgi_probe_data_uav_desc.buffer_offset = 0;
        ddgi_probe_data_uav_desc.buffer_size = ddgi_probe_data_buffer->GetDesc().buffer_desc.size;
        ddgi_probe_data_uav_desc.buffer_stride = sizeof(float4);
        if (!device->CreateSubresource(*ddgi_probe_data_buffer, ddgi_probe_data_uav_desc, &ddgi_probe_data_buffer_uav))
        {
            backlog::Post("failed to create ddgi probe data uav", backlog::LogLevel::Error);
            ddgi_probe_data_buffer = nullptr;
            return false;
        }

        ddgi_probe_data_history_buffer = device->CreateBuffer(ddgi_probe_data_buffer_desc);
        if (!ddgi_probe_data_history_buffer)
        {
            backlog::Post("failed to create ddgi probe data history buffer", backlog::LogLevel::Error);
            return false;
        }
        ddgi_probe_data_history_buffer->SetName("DDGI Probe Data History Buffer");
        if (!device->CreateSubresource(*ddgi_probe_data_history_buffer, ddgi_probe_data_srv_desc, &ddgi_probe_data_history_buffer_srv))
        {
            backlog::Post("failed to create ddgi probe data history srv", backlog::LogLevel::Error);
            ddgi_probe_data_history_buffer = nullptr;
            return false;
        }

        if (debug_options.ddgi_debug_enable)
        {
            RHIBufferDesc probe_data_readback_buffer_desc = {};
            probe_data_readback_buffer_desc.size = ddgi_probe_data_buffer_desc.size;
            probe_data_readback_buffer_desc.usage = RHIResourceUsage::Readback;
            debug_state.ddgi.probe_data_readback_buffer = device->CreateBuffer(probe_data_readback_buffer_desc);
            if (!debug_state.ddgi.probe_data_readback_buffer)
            {
                backlog::Post("failed to create ddgi debug probe data readback buffer", backlog::LogLevel::Error);
                return false;
            }
            debug_state.ddgi.probe_data_readback_buffer->SetName("DDGI Debug Probe Data Readback Buffer");
        }
        debug_state.ddgi.probe_data_readback_valid = false;

        ddgi_probe_counts = ddgi_volume.probe_counts;
        ddgi_probe_spacing = ddgi_volume.probe_spacing;
        ddgi_volume_min = ddgi_volume.volume_min;
        ddgi_max_distance = ddgi_volume.max_distance;
        ddgi_probe_update_offset = 0;
        return true;
    }

    bool RendererInternal::CreateShadowMapAtlasResources(FrameContext& frame_context, const Scene::RenderData& render_data)
    {
        if (render_data.shadow_map_atlas_size.x == 0 || render_data.shadow_map_atlas_size.y == 0)
        {
            frame_context.RemoveResourceDeferred(shadow_map_atlas);
            shadow_map_atlas_dsv = {};
            shadow_map_atlas_srv = {};
            shadow_map_atlas_size = { 0, 0 };
            return true;
        }

        const bool recreate_shadowmap_atlas =
            !shadow_map_atlas ||
            !shadow_map_atlas_dsv.IsValid() ||
            !shadow_map_atlas_srv.IsValid() ||
            shadow_map_atlas_size.x != render_data.shadow_map_atlas_size.x ||
            shadow_map_atlas_size.y != render_data.shadow_map_atlas_size.y;

        if (!recreate_shadowmap_atlas)
        {
            return true;
        }

        frame_context.RemoveResourceDeferred(shadow_map_atlas);
        RHITextureDesc shadow_map_atlas_desc = {};
        shadow_map_atlas_desc.width = render_data.shadow_map_atlas_size.x;
        shadow_map_atlas_desc.height = render_data.shadow_map_atlas_size.y;
        shadow_map_atlas_desc.depth = 1;
        shadow_map_atlas_desc.mip_levels = 1;
        shadow_map_atlas_desc.array_layers = 1;
        shadow_map_atlas_desc.sample_count = 1;
        shadow_map_atlas_desc.format = RHIFormat::D32Float;
        shadow_map_atlas_desc.usage = RHIResourceUsage::Default;
        shadow_map_atlas_desc.bind_flags = RHIBindFlags::DepthStencil | RHIBindFlags::ShaderResource;
        shadow_map_atlas = device->CreateTexture(shadow_map_atlas_desc);
        if (!shadow_map_atlas)
        {
            backlog::Post("failed to create shadow map atlas", backlog::LogLevel::Error);
            return false;
        }
        shadow_map_atlas->SetName("Shadow Map Atlas");

        shadow_map_atlas_dsv = {};
        RHISubresourceDesc shadow_map_atlas_subresource_desc = {};
        shadow_map_atlas_subresource_desc.type = RHISubresourceType::DepthStencil;
        shadow_map_atlas_subresource_desc.format = shadow_map_atlas_desc.format;
        if (!device->CreateSubresource(*shadow_map_atlas, shadow_map_atlas_subresource_desc, &shadow_map_atlas_dsv))
        {
            backlog::Post("failed to create shadow map atlas subresource", backlog::LogLevel::Error);
            shadow_map_atlas = nullptr;
            return false;
        }
        shadow_map_atlas_subresource_desc.type = RHISubresourceType::ShaderResource;
        if (!device->CreateSubresource(*shadow_map_atlas, shadow_map_atlas_subresource_desc, &shadow_map_atlas_srv))
        {
            backlog::Post("failed to create shadow map atlas subresource", backlog::LogLevel::Error);
            shadow_map_atlas = nullptr;
            return false;
        }

        shadow_map_atlas_size = render_data.shadow_map_atlas_size;
        return true;
    }

    bool RendererInternal::CreateRenderTargetResources(FrameContext& frame_context)
    {
        std::shared_ptr<RHISwapchain> swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            swapchain = device->CreateSwapchain(*current_window);
            if (!swapchain)
            {
                backlog::Post("failed to create swapchain", backlog::LogLevel::Error);
                return false;
            }
            current_window->SetRHISwapchain(swapchain);
        }

        std::shared_ptr<RHIResource> back_buffer = swapchain->GetCurrentBackBuffer();
        if (!back_buffer)
        {
            backlog::Post("failed to get swapchain back buffer", backlog::LogLevel::Error);
            return false;
        }

        for (uint32 i = 0; i < swapchain->GetBackBufferCount() && i < static_cast<uint32>(back_buffers_rtv.size()); ++i)
        {
            if (back_buffers_rtv[i].IsValid())
            {
                continue;
            }

            std::shared_ptr<RHIResource> swapchain_back_buffer = swapchain->GetBackBuffer(i);
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

        const RHIResourceDesc& back_buffer_desc = back_buffer->GetDesc();
        const RHITextureDesc& back_buffer_texture_desc = back_buffer_desc.texture_desc;
        const uint32 target_sample_count = back_buffer_texture_desc.sample_count > 0 ? back_buffer_texture_desc.sample_count : 1;
        bool recreate_depth_buffer = !depth_buffer || !depth_buffer_dsv.IsValid();
        if (!recreate_depth_buffer)
        {
            const RHITextureDesc& depth_texture_desc = depth_buffer->GetDesc().texture_desc;
            recreate_depth_buffer =
                depth_texture_desc.width != back_buffer_texture_desc.width ||
                depth_texture_desc.height != back_buffer_texture_desc.height ||
                depth_texture_desc.sample_count != target_sample_count ||
                depth_texture_desc.format != RHIFormat::D32Float;
        }

        if (recreate_depth_buffer)
        {
            frame_context.RemoveResourceDeferred(depth_buffer);
            RHITextureDesc depth_desc = {};
            depth_desc.width = back_buffer_texture_desc.width;
            depth_desc.height = back_buffer_texture_desc.height;
            depth_desc.depth = 1;
            depth_desc.mip_levels = 1;
            depth_desc.array_layers = 1;
            depth_desc.sample_count = target_sample_count;
            depth_desc.format = RHIFormat::D32Float;
            depth_desc.usage = RHIResourceUsage::Default;
            depth_desc.bind_flags = RHIBindFlags::DepthStencil;
            depth_buffer = device->CreateTexture(depth_desc);
            if (!depth_buffer)
            {
                backlog::Post("failed to create depth buffer", backlog::LogLevel::Error);
                return false;
            }
            depth_buffer->SetName("Scene Depth Buffer");

            depth_buffer_dsv = {};
            RHISubresourceDesc depth_subresource_desc = {};
            depth_subresource_desc.type = RHISubresourceType::DepthStencil;
            depth_subresource_desc.format = depth_desc.format;
            if (!device->CreateSubresource(*depth_buffer, depth_subresource_desc, &depth_buffer_dsv))
            {
                backlog::Post("failed to create depth buffer subresource", backlog::LogLevel::Error);
                depth_buffer = nullptr;
                return false;
            }
        }
        return true;
    }

    bool RendererInternal::UpdateSceneGPUData(FrameContext& frame_context, const Scene::RenderData& render_data, RHICommandList& command_list)
    {
        const Vector<ShaderInstance>& shader_instances = render_data.shader_instances;
        const Vector<ShaderGeometry>& shader_geometries = render_data.shader_geometries;
        const Vector<ShaderMaterial>& shader_materials = render_data.shader_materials;
        const Vector<float4>& shader_bone_matrices = render_data.shader_bone_matrices;
        const Vector<ShaderLight>& shader_lights = render_data.shader_lights;
        const Vector<ShaderShadowCascade>& shader_shadow_cascades = render_data.shader_shadow_cascades;
        const Vector<ShaderBVHNode>& shader_bvh_nodes = render_data.shader_bvh_nodes;
        const Vector<ShaderBVHInstance>& shader_bvh_instances = render_data.shader_bvh_instances;

        const Size required_instance_buffer_size = shader_instances.size() * sizeof(ShaderInstance);
        const Size required_geometry_buffer_size = shader_geometries.size() * sizeof(ShaderGeometry);
        const Size required_material_buffer_size = shader_materials.size() * sizeof(ShaderMaterial);
        const Size required_bone_matrix_buffer_size = shader_bone_matrices.size() * sizeof(float4);
        const Size required_light_buffer_size = shader_lights.size() * sizeof(ShaderLight);
        const Size required_shadow_cascade_buffer_size = shader_shadow_cascades.size() * sizeof(ShaderShadowCascade);
        const Size required_bvh_node_buffer_size = shader_bvh_nodes.size() * sizeof(ShaderBVHNode);
        const Size required_bvh_instance_buffer_size = shader_bvh_instances.size() * sizeof(ShaderBVHInstance);

        if (required_bone_matrix_buffer_size == 0)
        {
            frame_context.RemoveResourceDeferred(shader_bone_matrix_default_buffer);
            shader_bone_matrix_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_bone_matrix_default_buffer)
            {
                current_default_buffer_size = shader_bone_matrix_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_bone_matrix_default_buffer || current_default_buffer_size < required_bone_matrix_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_bone_matrix_default_buffer);
                RHIBufferDesc shader_bone_matrix_default_buffer_desc = {};
                shader_bone_matrix_default_buffer_desc.size = required_bone_matrix_buffer_size;
                shader_bone_matrix_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_bone_matrix_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_bone_matrix_default_buffer = device->CreateBuffer(shader_bone_matrix_default_buffer_desc);
                if (!shader_bone_matrix_default_buffer)
                {
                    backlog::Post("failed to create shader bone matrix default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_bone_matrix_default_buffer->SetName("Shader Bone Matrix Default Buffer");

                shader_bone_matrix_default_buffer_srv = {};
                RHISubresourceDesc shader_bone_matrix_default_subresource_desc = {};
                shader_bone_matrix_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_bone_matrix_default_subresource_desc.buffer_offset = 0;
                shader_bone_matrix_default_subresource_desc.buffer_size = shader_bone_matrix_default_buffer->GetDesc().buffer_desc.size;
                shader_bone_matrix_default_subresource_desc.buffer_stride = sizeof(float4);
                if (!device->CreateSubresource(*shader_bone_matrix_default_buffer, shader_bone_matrix_default_subresource_desc, &shader_bone_matrix_default_buffer_srv))
                {
                    backlog::Post("failed to create shader bone matrix default subresource", backlog::LogLevel::Error);
                    shader_bone_matrix_default_buffer = nullptr;
                    return false;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *shader_bone_matrix_default_buffer, shader_bone_matrices.data(), required_bone_matrix_buffer_size, RHIResourceState::ShaderRead, 0, command_list))
            {
                return false;
            }
        }

        if (required_instance_buffer_size == 0)
        {
            frame_context.shader_instance_upload_buffer = nullptr;
            frame_context.RemoveResourceDeferred(shader_instance_default_buffer);
            shader_instance_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_instance_default_buffer)
            {
                current_default_buffer_size = shader_instance_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_instance_default_buffer || current_default_buffer_size < required_instance_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_instance_default_buffer);
                RHIBufferDesc shader_instance_default_buffer_desc = {};
                shader_instance_default_buffer_desc.size = required_instance_buffer_size;
                shader_instance_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_instance_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_instance_default_buffer = device->CreateBuffer(shader_instance_default_buffer_desc);
                if (!shader_instance_default_buffer)
                {
                    backlog::Post("failed to create shader instance default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_instance_default_buffer->SetName("Shader Instance Default Buffer");

                shader_instance_default_buffer_srv = {};
                RHISubresourceDesc shader_instance_default_subresource_desc = {};
                shader_instance_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_instance_default_subresource_desc.buffer_offset = 0;
                shader_instance_default_subresource_desc.buffer_size = shader_instance_default_buffer->GetDesc().buffer_desc.size;
                shader_instance_default_subresource_desc.buffer_stride = sizeof(ShaderInstance);
                if (!device->CreateSubresource(*shader_instance_default_buffer, shader_instance_default_subresource_desc, &shader_instance_default_buffer_srv))
                {
                    backlog::Post("failed to create shader instance default subresource", backlog::LogLevel::Error);
                    shader_instance_default_buffer = nullptr;
                    return false;
                }
            }

            Size current_upload_buffer_size = 0;
            if (frame_context.shader_instance_upload_buffer)
            {
                current_upload_buffer_size = frame_context.shader_instance_upload_buffer->GetDesc().buffer_desc.size;
            }

            if (!frame_context.shader_instance_upload_buffer || current_upload_buffer_size < required_instance_buffer_size)
            {
                RHIBufferDesc shader_instance_upload_buffer_desc = {};
                shader_instance_upload_buffer_desc.size = required_instance_buffer_size;
                shader_instance_upload_buffer_desc.usage = RHIResourceUsage::Upload;
                shader_instance_upload_buffer_desc.bind_flags = RHIBindFlags::None;
                frame_context.shader_instance_upload_buffer = device->CreateBuffer(shader_instance_upload_buffer_desc);
                if (!frame_context.shader_instance_upload_buffer)
                {
                    backlog::Post("failed to create shader instance upload buffer", backlog::LogLevel::Error);
                    return false;
                }
                frame_context.shader_instance_upload_buffer->SetName("Shader Instance Upload Buffer");
            }

            Vector<ShaderInstance> patched_shader_instances = shader_instances;
            if (shader_bone_matrix_default_buffer_srv.IsValid())
            {
                for (ShaderInstance& shader_instance : patched_shader_instances)
                {
                    if (shader_instance.bone_count > 0)
                    {
                        shader_instance.bone_matrix_buffer_descriptor = shader_bone_matrix_default_buffer_srv.descriptor_index;
                    }
                }
            }

            void* mapped_data = frame_context.shader_instance_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped instance upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, patched_shader_instances.data(), required_instance_buffer_size);

            command_list.TransitionResource(*shader_instance_default_buffer, RHIResourceState::CopyDest);
            command_list.CopyResource(*shader_instance_default_buffer, *frame_context.shader_instance_upload_buffer);
            command_list.TransitionResource(*shader_instance_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_geometry_buffer_size == 0)
        {
            frame_context.shader_geometry_upload_buffer = nullptr;
            frame_context.RemoveResourceDeferred(shader_geometry_default_buffer);
            shader_geometry_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_geometry_default_buffer)
            {
                current_default_buffer_size = shader_geometry_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_geometry_default_buffer || current_default_buffer_size < required_geometry_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_geometry_default_buffer);
                RHIBufferDesc shader_geometry_default_buffer_desc = {};
                shader_geometry_default_buffer_desc.size = required_geometry_buffer_size;
                shader_geometry_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_geometry_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_geometry_default_buffer = device->CreateBuffer(shader_geometry_default_buffer_desc);
                if (!shader_geometry_default_buffer)
                {
                    backlog::Post("failed to create shader geometry default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_geometry_default_buffer->SetName("Shader Geometry Default Buffer");

                shader_geometry_default_buffer_srv = {};
                RHISubresourceDesc shader_geometry_default_subresource_desc = {};
                shader_geometry_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_geometry_default_subresource_desc.buffer_offset = 0;
                shader_geometry_default_subresource_desc.buffer_size = shader_geometry_default_buffer->GetDesc().buffer_desc.size;
                shader_geometry_default_subresource_desc.buffer_stride = sizeof(ShaderGeometry);
                if (!device->CreateSubresource(*shader_geometry_default_buffer, shader_geometry_default_subresource_desc, &shader_geometry_default_buffer_srv))
                {
                    backlog::Post("failed to create shader geometry default subresource", backlog::LogLevel::Error);
                    shader_geometry_default_buffer = nullptr;
                    return false;
                }
            }

            Size current_upload_buffer_size = 0;
            if (frame_context.shader_geometry_upload_buffer)
            {
                current_upload_buffer_size = frame_context.shader_geometry_upload_buffer->GetDesc().buffer_desc.size;
            }

            if (!frame_context.shader_geometry_upload_buffer || current_upload_buffer_size < required_geometry_buffer_size)
            {
                RHIBufferDesc shader_geometry_upload_buffer_desc = {};
                shader_geometry_upload_buffer_desc.size = required_geometry_buffer_size;
                shader_geometry_upload_buffer_desc.usage = RHIResourceUsage::Upload;
                shader_geometry_upload_buffer_desc.bind_flags = RHIBindFlags::None;
                frame_context.shader_geometry_upload_buffer = device->CreateBuffer(shader_geometry_upload_buffer_desc);
                if (!frame_context.shader_geometry_upload_buffer)
                {
                    backlog::Post("failed to create shader geometry upload buffer", backlog::LogLevel::Error);
                    return false;
                }
                frame_context.shader_geometry_upload_buffer->SetName("Shader Geometry Upload Buffer");
            }

            void* mapped_data = frame_context.shader_geometry_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped geometry upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_geometries.data(), required_geometry_buffer_size);

            command_list.TransitionResource(*shader_geometry_default_buffer, RHIResourceState::CopyDest);
            command_list.CopyResource(*shader_geometry_default_buffer, *frame_context.shader_geometry_upload_buffer);
            command_list.TransitionResource(*shader_geometry_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_material_buffer_size == 0)
        {
            frame_context.shader_material_upload_buffer = nullptr;
            frame_context.RemoveResourceDeferred(shader_material_default_buffer);
            shader_material_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_material_default_buffer)
            {
                current_default_buffer_size = shader_material_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_material_default_buffer || current_default_buffer_size < required_material_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_material_default_buffer);
                RHIBufferDesc shader_material_default_buffer_desc = {};
                shader_material_default_buffer_desc.size = required_material_buffer_size;
                shader_material_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_material_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_material_default_buffer = device->CreateBuffer(shader_material_default_buffer_desc);
                if (!shader_material_default_buffer)
                {
                    backlog::Post("failed to create shader material default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_material_default_buffer->SetName("Shader Material Default Buffer");

                shader_material_default_buffer_srv = {};
                RHISubresourceDesc shader_material_default_subresource_desc = {};
                shader_material_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_material_default_subresource_desc.buffer_offset = 0;
                shader_material_default_subresource_desc.buffer_size = shader_material_default_buffer->GetDesc().buffer_desc.size;
                shader_material_default_subresource_desc.buffer_stride = sizeof(ShaderMaterial);
                if (!device->CreateSubresource(*shader_material_default_buffer, shader_material_default_subresource_desc, &shader_material_default_buffer_srv))
                {
                    backlog::Post("failed to create shader material default subresource", backlog::LogLevel::Error);
                    shader_material_default_buffer = nullptr;
                    return false;
                }
            }

            Size current_upload_buffer_size = 0;
            if (frame_context.shader_material_upload_buffer)
            {
                current_upload_buffer_size = frame_context.shader_material_upload_buffer->GetDesc().buffer_desc.size;
            }

            if (!frame_context.shader_material_upload_buffer || current_upload_buffer_size < required_material_buffer_size)
            {
                RHIBufferDesc shader_material_upload_buffer_desc = {};
                shader_material_upload_buffer_desc.size = required_material_buffer_size;
                shader_material_upload_buffer_desc.usage = RHIResourceUsage::Upload;
                shader_material_upload_buffer_desc.bind_flags = RHIBindFlags::None;
                frame_context.shader_material_upload_buffer = device->CreateBuffer(shader_material_upload_buffer_desc);
                if (!frame_context.shader_material_upload_buffer)
                {
                    backlog::Post("failed to create shader material upload buffer", backlog::LogLevel::Error);
                    return false;
                }
                frame_context.shader_material_upload_buffer->SetName("Shader Material Upload Buffer");
            }

            void* mapped_data = frame_context.shader_material_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped material upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_materials.data(), required_material_buffer_size);

            command_list.TransitionResource(*shader_material_default_buffer, RHIResourceState::CopyDest);
            command_list.CopyResource(*shader_material_default_buffer, *frame_context.shader_material_upload_buffer);
            command_list.TransitionResource(*shader_material_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_light_buffer_size == 0)
        {
            frame_context.shader_light_upload_buffer = nullptr;
            frame_context.RemoveResourceDeferred(shader_light_default_buffer);
            shader_light_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_light_default_buffer)
            {
                current_default_buffer_size = shader_light_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_light_default_buffer || current_default_buffer_size < required_light_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_light_default_buffer);
                RHIBufferDesc shader_light_default_buffer_desc = {};
                shader_light_default_buffer_desc.size = required_light_buffer_size;
                shader_light_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_light_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_light_default_buffer = device->CreateBuffer(shader_light_default_buffer_desc);
                if (!shader_light_default_buffer)
                {
                    backlog::Post("failed to create shader light default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_light_default_buffer->SetName("Shader Light Default Buffer");

                shader_light_default_buffer_srv = {};
                RHISubresourceDesc shader_light_default_subresource_desc = {};
                shader_light_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_light_default_subresource_desc.buffer_offset = 0;
                shader_light_default_subresource_desc.buffer_size = shader_light_default_buffer->GetDesc().buffer_desc.size;
                shader_light_default_subresource_desc.buffer_stride = sizeof(ShaderLight);
                if (!device->CreateSubresource(*shader_light_default_buffer, shader_light_default_subresource_desc, &shader_light_default_buffer_srv))
                {
                    backlog::Post("failed to create shader light default subresource", backlog::LogLevel::Error);
                    shader_light_default_buffer = nullptr;
                    return false;
                }
            }

            Size current_upload_buffer_size = 0;
            if (frame_context.shader_light_upload_buffer)
            {
                current_upload_buffer_size = frame_context.shader_light_upload_buffer->GetDesc().buffer_desc.size;
            }

            if (!frame_context.shader_light_upload_buffer || current_upload_buffer_size < required_light_buffer_size)
            {
                RHIBufferDesc shader_light_upload_buffer_desc = {};
                shader_light_upload_buffer_desc.size = required_light_buffer_size;
                shader_light_upload_buffer_desc.usage = RHIResourceUsage::Upload;
                shader_light_upload_buffer_desc.bind_flags = RHIBindFlags::None;
                frame_context.shader_light_upload_buffer = device->CreateBuffer(shader_light_upload_buffer_desc);
                if (!frame_context.shader_light_upload_buffer)
                {
                    backlog::Post("failed to create shader light upload buffer", backlog::LogLevel::Error);
                    return false;
                }
                frame_context.shader_light_upload_buffer->SetName("Shader Light Upload Buffer");
            }

            void* mapped_data = frame_context.shader_light_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped light upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_lights.data(), required_light_buffer_size);

            command_list.TransitionResource(*shader_light_default_buffer, RHIResourceState::CopyDest);
            command_list.CopyResource(*shader_light_default_buffer, *frame_context.shader_light_upload_buffer);
            command_list.TransitionResource(*shader_light_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_shadow_cascade_buffer_size == 0)
        {
            frame_context.RemoveResourceDeferred(shader_shadow_cascade_default_buffer);
            shader_shadow_cascade_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_shadow_cascade_default_buffer)
            {
                current_default_buffer_size = shader_shadow_cascade_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_shadow_cascade_default_buffer || current_default_buffer_size < required_shadow_cascade_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_shadow_cascade_default_buffer);
                RHIBufferDesc shader_shadow_cascade_default_buffer_desc = {};
                shader_shadow_cascade_default_buffer_desc.size = required_shadow_cascade_buffer_size;
                shader_shadow_cascade_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_shadow_cascade_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_shadow_cascade_default_buffer = device->CreateBuffer(shader_shadow_cascade_default_buffer_desc);
                if (!shader_shadow_cascade_default_buffer)
                {
                    backlog::Post("failed to create shader shadow cascade default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_shadow_cascade_default_buffer->SetName("Shader Shadow Cascade Default Buffer");

                shader_shadow_cascade_default_buffer_srv = {};
                RHISubresourceDesc shader_shadow_cascade_default_subresource_desc = {};
                shader_shadow_cascade_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_shadow_cascade_default_subresource_desc.buffer_offset = 0;
                shader_shadow_cascade_default_subresource_desc.buffer_size = shader_shadow_cascade_default_buffer->GetDesc().buffer_desc.size;
                shader_shadow_cascade_default_subresource_desc.buffer_stride = sizeof(ShaderShadowCascade);
                if (!device->CreateSubresource(*shader_shadow_cascade_default_buffer, shader_shadow_cascade_default_subresource_desc, &shader_shadow_cascade_default_buffer_srv))
                {
                    backlog::Post("failed to create shader shadow cascade default subresource", backlog::LogLevel::Error);
                    shader_shadow_cascade_default_buffer = nullptr;
                    return false;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *shader_shadow_cascade_default_buffer, shader_shadow_cascades.data(), required_shadow_cascade_buffer_size, RHIResourceState::ShaderRead, 0, command_list))
            {
                return false;
            }
        }

        if (required_bvh_node_buffer_size == 0)
        {
            frame_context.RemoveResourceDeferred(shader_bvh_node_default_buffer);
            shader_bvh_node_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_bvh_node_default_buffer)
            {
                current_default_buffer_size = shader_bvh_node_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_bvh_node_default_buffer || current_default_buffer_size < required_bvh_node_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_bvh_node_default_buffer);
                RHIBufferDesc shader_bvh_node_default_buffer_desc = {};
                shader_bvh_node_default_buffer_desc.size = required_bvh_node_buffer_size;
                shader_bvh_node_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_bvh_node_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_bvh_node_default_buffer = device->CreateBuffer(shader_bvh_node_default_buffer_desc);
                if (!shader_bvh_node_default_buffer)
                {
                    backlog::Post("failed to create shader bvh node default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_bvh_node_default_buffer->SetName("Shader BVH Node Default Buffer");

                shader_bvh_node_default_buffer_srv = {};
                RHISubresourceDesc shader_bvh_node_default_subresource_desc = {};
                shader_bvh_node_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_bvh_node_default_subresource_desc.buffer_offset = 0;
                shader_bvh_node_default_subresource_desc.buffer_size = shader_bvh_node_default_buffer->GetDesc().buffer_desc.size;
                shader_bvh_node_default_subresource_desc.buffer_stride = sizeof(ShaderBVHNode);
                if (!device->CreateSubresource(*shader_bvh_node_default_buffer, shader_bvh_node_default_subresource_desc, &shader_bvh_node_default_buffer_srv))
                {
                    backlog::Post("failed to create shader bvh node default subresource", backlog::LogLevel::Error);
                    shader_bvh_node_default_buffer = nullptr;
                    return false;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *shader_bvh_node_default_buffer, shader_bvh_nodes.data(), required_bvh_node_buffer_size, RHIResourceState::ShaderRead, 0, command_list))
            {
                return false;
            }
        }

        if (required_bvh_instance_buffer_size == 0)
        {
            frame_context.RemoveResourceDeferred(shader_bvh_instance_default_buffer);
            shader_bvh_instance_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_bvh_instance_default_buffer)
            {
                current_default_buffer_size = shader_bvh_instance_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_bvh_instance_default_buffer || current_default_buffer_size < required_bvh_instance_buffer_size)
            {
                frame_context.RemoveResourceDeferred(shader_bvh_instance_default_buffer);
                RHIBufferDesc shader_bvh_instance_default_buffer_desc = {};
                shader_bvh_instance_default_buffer_desc.size = required_bvh_instance_buffer_size;
                shader_bvh_instance_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_bvh_instance_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_bvh_instance_default_buffer = device->CreateBuffer(shader_bvh_instance_default_buffer_desc);
                if (!shader_bvh_instance_default_buffer)
                {
                    backlog::Post("failed to create shader bvh instance default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_bvh_instance_default_buffer->SetName("Shader BVH Instance Default Buffer");

                shader_bvh_instance_default_buffer_srv = {};
                RHISubresourceDesc shader_bvh_instance_default_subresource_desc = {};
                shader_bvh_instance_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_bvh_instance_default_subresource_desc.buffer_offset = 0;
                shader_bvh_instance_default_subresource_desc.buffer_size = shader_bvh_instance_default_buffer->GetDesc().buffer_desc.size;
                shader_bvh_instance_default_subresource_desc.buffer_stride = sizeof(ShaderBVHInstance);
                if (!device->CreateSubresource(*shader_bvh_instance_default_buffer, shader_bvh_instance_default_subresource_desc, &shader_bvh_instance_default_buffer_srv))
                {
                    backlog::Post("failed to create shader bvh instance default subresource", backlog::LogLevel::Error);
                    shader_bvh_instance_default_buffer = nullptr;
                    return false;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *shader_bvh_instance_default_buffer, shader_bvh_instances.data(), required_bvh_instance_buffer_size, RHIResourceState::ShaderRead, 0, command_list))
            {
                return false;
            }
        }

        return true;
    }

    bool RendererInternal::UpdateFrameConstants(FrameContext& frame_context, const View& view, const Scene::RenderData& render_data, RHICommandList& command_list)
    {
        ShaderFrame shader_frame{};
        shader_frame.Init();
        shader_frame.scene.instancebuffer = shader_instance_default_buffer_srv.descriptor_index;
        shader_frame.scene.geometrybuffer = shader_geometry_default_buffer_srv.descriptor_index;
        shader_frame.scene.materialbuffer = shader_material_default_buffer_srv.descriptor_index;
        shader_frame.scene.lightbuffer = shader_light_default_buffer_srv.descriptor_index;
        shader_frame.scene.lights = render_data.forward_light_mask;
        shader_frame.scene.shadow_atlas = shadow_map_atlas_srv.descriptor_index;
        shader_frame.scene.shadow_cascade_buffer = shader_shadow_cascade_default_buffer_srv.descriptor_index;
        shader_frame.scene.bvh_node_buffer = shader_bvh_node_default_buffer_srv.descriptor_index;
        shader_frame.scene.bvh_instance_buffer = shader_bvh_instance_default_buffer_srv.descriptor_index;
        shader_frame.scene.bvh_node_count = static_cast<uint32>(render_data.shader_bvh_nodes.size());
        shader_frame.scene.bvh_instance_count = static_cast<uint32>(render_data.shader_bvh_instances.size());
        shader_frame.sky = render_data.shader_sky;
        shader_frame.environment_lighting = render_data.shader_environment_lighting;
        shader_frame.ddgi_volume = render_data.shader_ddgi_volume;
        shader_frame.ddgi_volume.irradiance_texture = ddgi_irradiance_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.irradiance_texture_uav = ddgi_irradiance_texture_uav.descriptor_index;
        shader_frame.ddgi_volume.visibility_texture = ddgi_visibility_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.visibility_texture_uav = ddgi_visibility_texture_uav.descriptor_index;
        shader_frame.ddgi_volume.probe_data_buffer = ddgi_probe_data_buffer_srv.descriptor_index;
        shader_frame.ddgi_volume.probe_data_buffer_uav = ddgi_probe_data_buffer_uav.descriptor_index;
        shader_frame.ddgi_volume.previous_irradiance_texture = ddgi_irradiance_history_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.previous_visibility_texture = ddgi_visibility_history_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.previous_probe_data_buffer = ddgi_probe_data_history_buffer_srv.descriptor_index;
        shader_frame.ddgi_volume.history_valid = ddgi_history_valid ? 1u : 0u;
        shader_frame.ddgi_volume.probe_update_start = shader_frame.ddgi_volume.total_probe_count > 0 ? ddgi_probe_update_offset % shader_frame.ddgi_volume.total_probe_count : 0;
        shader_frame.ddgi_volume.probes_per_frame = ddgi_history_valid ? (std::min)(shader_frame.ddgi_volume.probes_per_frame, shader_frame.ddgi_volume.total_probe_count) : shader_frame.ddgi_volume.total_probe_count;
        shader_frame.ddgi_volume.probe_update_dispatch_width = (std::min)(shader_frame.ddgi_volume.probes_per_frame, 65535u);

        ShaderCamera shader_camera{};
        shader_camera.Init();
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
                shader_camera.view = camera_component->view;
                shader_camera.projection = camera_component->projection;
                shader_camera.view_projection = camera_component->view_projection;
                shader_camera.inv_view_projection = camera_component->inv_view_projection;
            }
        }

        if (!UpdateDefaultBuffer(frame_context, *shader_frame_buffer, &shader_frame, sizeof(ShaderFrame), RHIResourceState::ConstantBuffer, 0, command_list))
        {
            return false;
        }
        if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shader_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer, 0, command_list))
        {
            return false;
        }

        return true;
    }

    bool RendererInternal::BuildShadowCascades(const View& view)
    {
        if (!view.scene || view.camera_entity == INVALID_ENTITY)
        {
            return false;
        }

        Scene::RenderData& render_data = view.scene->GetRenderData();
        render_data.shader_shadow_cascades.clear();
        render_data.render_shadow_slices.clear();
        render_data.shadow_map_atlas_size = { 0, 0 };

        const ecs::CameraComponent* camera = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
        if (!camera)
        {
            return true;
        }

        auto light_array = view.scene->GetComponentArray<ecs::LightComponent>().get();
        if (!light_array)
        {
            return true;
        }

        const uint32 light_count = (std::min)(static_cast<uint32>(light_array->GetSize()), static_cast<uint32>(render_data.shader_lights.size()));
        rectpacker::State atlas_packer = {};

        for (uint32 light_index = 0u; light_index < light_count; ++light_index)
        {
            const ecs::LightComponent& light = light_array->data[light_index];
            ShaderLight& shader_light = render_data.shader_lights[light_index];
            shader_light.shadow_slice_offset = 0u;
            shader_light.shadow_slice_count = 0u;

            if (!light.IsActive() || !light.IsDynamic() || !light.IsCastShadow())
            {
                continue;
            }

            if (light.type != ecs::LightComponent::Directional)
            {
                continue;
            }

            const uint32 cascade_count = camera->IsOrtho() ? 1u : (std::min)(light.shadow_cascade_count, SHADOW_CASCADE_COUNT_MAX);
            if (cascade_count == 0)
            {
                continue;
            }

            const uint32 cascade_offset = static_cast<uint32>(render_data.shader_shadow_cascades.size());
            shader_light.shadow_slice_offset = cascade_offset;
            shader_light.shadow_slice_count = cascade_count;

            float split_distances[SHADOW_CASCADE_COUNT_MAX + 1] = {};
            split_distances[0] = camera->near_plane;
            for (uint32 cascade_index = 1; cascade_index <= cascade_count; ++cascade_index)
            {
                const float t = static_cast<float>(cascade_index) / static_cast<float>(cascade_count);
                const float uniform_split = math::Lerp(camera->near_plane, camera->far_plane, t);
                const float log_split = camera->near_plane * std::pow(camera->far_plane / camera->near_plane, t);
                split_distances[cascade_index] = math::Lerp(uniform_split, log_split, light.shadow_cascade_lambda);
            }
            split_distances[cascade_count] = camera->far_plane;

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

                math::AABB frustum_light_bound = {};
                frustum_light_bound.Invalidate();
                for (const float3& corner : frustum_corners)
                {
                    float3 transformed_corner = {};
                    XMStoreFloat3(&transformed_corner, XMVector3TransformCoord(XMLoadFloat3(&corner), shadow_view));

                    frustum_light_bound.min = math::Min(frustum_light_bound.min, transformed_corner);
                    frustum_light_bound.max = math::Max(frustum_light_bound.max, transformed_corner);
                }

                math::AABB caster_light_bound = {};
                caster_light_bound.Invalidate();
                if (render_data.shadow_caster_world_bound.IsValid())
                {
                    caster_light_bound = render_data.shadow_caster_world_bound.TransformAABB(shadow_view);
                }

                float3 cascade_center_ls = frustum_light_bound.GetCenter();
                float3 cascade_extent_ls = frustum_light_bound.GetExtent();

                const uint32 shadow_resolution = (std::max)(1u, light.shadow_map_resolution);
                const float cascade_width = (std::max)(cascade_extent_ls.x * 2.0f, 0.001f);
                const float cascade_height = (std::max)(cascade_extent_ls.y * 2.0f, 0.001f);
                const float texel_size_x = cascade_width / static_cast<float>(shadow_resolution);
                const float texel_size_y = cascade_height / static_cast<float>(shadow_resolution);

                cascade_center_ls.x = std::floor(cascade_center_ls.x / texel_size_x) * texel_size_x;
                cascade_center_ls.y = std::floor(cascade_center_ls.y / texel_size_y) * texel_size_y;

                const float min_x = cascade_center_ls.x - cascade_extent_ls.x;
                const float max_x = cascade_center_ls.x + cascade_extent_ls.x;
                const float min_y = cascade_center_ls.y - cascade_extent_ls.y;
                const float max_y = cascade_center_ls.y + cascade_extent_ls.y;

                float near_z = frustum_light_bound.max.z + 10.0f;
                float far_z = frustum_light_bound.min.z - 10.0f;
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
                render_data.shader_shadow_cascades.push_back(shader_shadow_cascade);

                Scene::RenderData::RenderShadowSlice render_shadow_slice = {};
                render_shadow_slice.light_index = light_index;
                render_shadow_slice.view_projection = shader_shadow_cascade.shadow_view_projection;
                render_data.render_shadow_slices.push_back(render_shadow_slice);

                rectpacker::Rect rect = {};
                rect.id = static_cast<int>(render_data.shader_shadow_cascades.size() - 1);
                rect.w = static_cast<stbrp_coord>(shadow_resolution);
                rect.h = static_cast<stbrp_coord>(shadow_resolution);
                atlas_packer.AddRect(rect);
            }
        }

        if (atlas_packer.rects.empty())
        {
            return true;
        }

        if (!atlas_packer.Pack(16384))
        {
            backlog::Post("failed to pack shadow map atlas", backlog::LogLevel::Error);
            return false;
        }

        render_data.shadow_map_atlas_size = {
            static_cast<uint32>(atlas_packer.width),
            static_cast<uint32>(atlas_packer.height)
        };

        for (const rectpacker::Rect& rect : atlas_packer.rects)
        {
            if (rect.was_packed == 0 || rect.id < 0)
            {
                continue;
            }

            ShaderShadowCascade& shader_shadow_cascade = render_data.shader_shadow_cascades[rect.id];
            shader_shadow_cascade.shadow_atlas_scale_bias = {
                static_cast<float>(rect.w) / static_cast<float>(render_data.shadow_map_atlas_size.x),
                static_cast<float>(rect.h) / static_cast<float>(render_data.shadow_map_atlas_size.y),
                static_cast<float>(rect.x) / static_cast<float>(render_data.shadow_map_atlas_size.x),
                static_cast<float>(rect.y) / static_cast<float>(render_data.shadow_map_atlas_size.y)
            };

            Scene::RenderData::RenderShadowSlice& render_shadow_slice = render_data.render_shadow_slices[rect.id];
            render_shadow_slice.shadow_map_atlas_rect = { rect.x, rect.y, rect.w, rect.h };
        }

        return true;
    }

    bool RendererInternal::DrawScene(const FrameContext& frame_context, const View& view, RenderPassType pass, uint32 flags, RHICommandList& command_list)
    {
        const Scene::RenderData& render_data = view.scene->GetRenderData();

        resource::ShaderLibrary& shader_library = GetShaderLibrary();
        RHICompareOp depth_compare = RHICompareOp::GreaterEqual;
        const bool draw_wireframe = pass == RenderPassType::MainPass && debug_options.wireframe_enable;
        const bool draw_primitives = pass == RenderPassType::MainPass && (flags & DrawScene_Primitive) != 0;
        if (pass == RenderPassType::MainPass && !draw_wireframe)
        {
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

        RHISubresourceBinding shader_camera_binding = {};
        shader_camera_binding.resource = shader_camera_buffer.get();
        shader_camera_binding.subresource = shader_camera_buffer_cbv;

        if ((flags & (DrawScene_Opaque | DrawScene_Transparent)) != 0 && !render_data.mesh_renderables.empty())
        {
            GraphicsPipelineHash current_pipeline_hash = {};
            bool has_current_pipeline = false;

            for (const auto& renderable : render_data.mesh_renderables)
            {
                if (renderable.IsTransparent())
                {
                    if ((flags & DrawScene_Transparent) == 0)
                    {
                        continue;
                    }
                }
                else
                {
                    if ((flags & DrawScene_Opaque) == 0)
                    {
                        continue;
                    }
                }

                if (pass == RenderPassType::ShadowPass && !renderable.IsCastShadow())
                {
                    continue;
                }

                // TODO: reduce pso switch
                GraphicsPipelineHash renderable_pipeline_hash = pipeline_hash;
                if (pass == RenderPassType::MainPass)
                {
                    renderable_pipeline_hash.storage.bits.shader_type = draw_wireframe ? SHADER_MATERIAL_TYPE_UNLIT : renderable.shader_type;
                }
                if (!has_current_pipeline || !(current_pipeline_hash == renderable_pipeline_hash))
                {
                    std::shared_ptr<RHIPipeline> pipeline = shader_library.GetPipeline(renderable_pipeline_hash);
                    if (!pipeline)
                    {
                        continue;
                    }
                    command_list.SetGraphicsPipeline(*pipeline);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);
                    current_pipeline_hash = renderable_pipeline_hash;
                    has_current_pipeline = true;
                }

                command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_offset, renderable.index_count * sizeof(uint32));
                command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                command_list.PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                command_list.DrawIndexed(renderable.index_count, 1, 0, 0, 0);
            }
        }

        if ((flags & (DrawScene_Opaque | DrawScene_Transparent)) != 0 && !render_data.double_sided_renderables.empty())
        {
            GraphicsPipelineHash double_sided_pipeline_hash = pipeline_hash;
            double_sided_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            GraphicsPipelineHash current_pipeline_hash = {};
            bool has_current_pipeline = false;

            for (const auto& renderable : render_data.double_sided_renderables)
            {
                if (renderable.IsTransparent())
                {
                    if ((flags & DrawScene_Transparent) == 0)
                    {
                        continue;
                    }
                }
                else
                {
                    if ((flags & DrawScene_Opaque) == 0)
                    {
                        continue;
                    }
                }

                if (pass == RenderPassType::ShadowPass && !renderable.IsCastShadow())
                {
                    continue;
                }

                // TODO: reduce pso switch
                GraphicsPipelineHash renderable_pipeline_hash = double_sided_pipeline_hash;
                if (pass == RenderPassType::MainPass)
                {
                    renderable_pipeline_hash.storage.bits.shader_type = draw_wireframe ? SHADER_MATERIAL_TYPE_UNLIT : renderable.shader_type;
                }
                if (!has_current_pipeline || !(current_pipeline_hash == renderable_pipeline_hash))
                {
                    std::shared_ptr<RHIPipeline> pipeline = shader_library.GetPipeline(renderable_pipeline_hash);
                    if (!pipeline)
                    {
                        continue;
                    }
                    command_list.SetGraphicsPipeline(*pipeline);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);
                    current_pipeline_hash = renderable_pipeline_hash;
                    has_current_pipeline = true;
                }

                command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_offset, renderable.index_count * sizeof(uint32));
                command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                command_list.PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                command_list.DrawIndexed(renderable.index_count, 1, 0, 0, 0);
            }
        }

        if (draw_primitives)
        {
            GraphicsPipelineHash line_pipeline_hash = pipeline_hash;
            line_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::LineList);
            line_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            line_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            line_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

            std::shared_ptr<RHIPipeline> line_pipeline = shader_library.GetPipeline(line_pipeline_hash);
            if (line_pipeline)
            {
                command_list.SetGraphicsPipeline(*line_pipeline);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);

                for (const auto& renderable : render_data.line_renderables)
                {
                    if (renderable.IsTransparent())
                    {
                        if ((flags & DrawScene_Transparent) == 0)
                        {
                            continue;
                        }
                    }
                    else
                    {
                        if ((flags & DrawScene_Opaque) == 0)
                        {
                            continue;
                        }
                    }

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

            std::shared_ptr<RHIPipeline> point_pipeline = shader_library.GetPipeline(point_pipeline_hash);
            if (point_pipeline)
            {
                command_list.SetGraphicsPipeline(*point_pipeline);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);

                for (const auto& renderable : render_data.point_renderables)
                {
                    if (renderable.IsTransparent())
                    {
                        if ((flags & DrawScene_Transparent) == 0)
                        {
                            continue;
                        }
                    }
                    else
                    {
                        if ((flags & DrawScene_Opaque) == 0)
                        {
                            continue;
                        }
                    }

                    command_list.SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_offset, renderable.index_count * sizeof(uint32));
                    command_list.SetPrimitiveTopology(ToRHIPrimitiveTopology(renderable.primitive_topology));
                    command_list.PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                    command_list.DrawIndexed(renderable.index_count, 1, 0, 0, 0);
                }
            }
        }

        if (pass == RenderPassType::Sprite3DPass && (flags & DrawScene_3DSprite) != 0 && (!render_data.sprite_3d_renderables.empty() || !render_data.text_3d_renderables.empty()))
        {
            struct SpriteTextDrawItem
            {
                enum Type : uint32
                {
                    Sprite,
                    Text
                };

                Type type = Sprite;
                Size index = 0;
                float distance_sq = 0.0f;
            };

            GraphicsPipelineHash sprite_pipeline_hash = {};
            sprite_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite3DPass);
            sprite_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            sprite_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            sprite_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            sprite_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
            sprite_pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite3DPassMode::Sprite);

            GraphicsPipelineHash text_pipeline_hash = {};
            text_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite3DPass);
            text_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            text_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            text_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            text_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
            text_pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite3DPassMode::Text);

            std::shared_ptr<RHIPipeline> sprite_pipeline = shader_library.GetPipeline(sprite_pipeline_hash);
            std::shared_ptr<RHIPipeline> text_pipeline = shader_library.GetPipeline(text_pipeline_hash);
            if (!sprite_pipeline || !text_pipeline)
            {
                return false;
            }

            float3 camera_position = {};
            if (const ecs::CameraComponent* camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity))
            {
                camera_position = camera_component->eye;
            }

            auto get_distance_sq = [&](uint32 instance_index, const float2& local_center)
            {
                if (instance_index >= render_data.shader_instances.size())
                {
                    return 0.0f;
                }

                float3 world_center = {};
                const ShaderInstance& instance = render_data.shader_instances[instance_index];
                XMVECTOR xcenter = XMVectorSet(local_center.x, local_center.y, 0.0f, 1.0f);
                XMMATRIX xworld = XMLoadFloat4x4(&instance.world_transform);
                XMStoreFloat3(&world_center, XMVector3Transform(xcenter, xworld));
                const float3 to_camera = { world_center.x - camera_position.x, world_center.y - camera_position.y, world_center.z - camera_position.z };
                return to_camera.x * to_camera.x + to_camera.y * to_camera.y + to_camera.z * to_camera.z;
            };

            Vector<SpriteTextDrawItem> draw_items;
            if (!render_data.sprite_3d_renderables.empty())
            {
                draw_items.reserve(draw_items.size() + render_data.sprite_3d_renderables.size());
                for (Size sprite_index = 0; sprite_index < render_data.sprite_3d_renderables.size(); ++sprite_index)
                {
                    const Scene::RenderData::Sprite3DRenderable& renderable = render_data.sprite_3d_renderables[sprite_index];
                    const float2 local_center = { (0.5f - renderable.pivot.x) * renderable.size.x, (0.5f - renderable.pivot.y) * renderable.size.y };
                    draw_items.push_back({ SpriteTextDrawItem::Sprite, sprite_index, get_distance_sq(renderable.instance_index, local_center) });
                }
            }
            if (!render_data.text_3d_renderables.empty())
            {
                draw_items.reserve(draw_items.size() + render_data.text_3d_renderables.size());
                for (Size text_index = 0; text_index < render_data.text_3d_renderables.size(); ++text_index)
                {
                    const Scene::RenderData::Text3DRenderable& renderable = render_data.text_3d_renderables[text_index];
                    const float2 local_center = { renderable.position.x + renderable.size.x * 0.5f, renderable.position.y + renderable.size.y * 0.5f };
                    draw_items.push_back({ SpriteTextDrawItem::Text, text_index, get_distance_sq(renderable.instance_index, local_center) });
                }
            }

            std::sort(draw_items.begin(), draw_items.end(), [](const SpriteTextDrawItem& lhs, const SpriteTextDrawItem& rhs) {
                return lhs.distance_sq > rhs.distance_sq;
            });

            command_list.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            SpriteTextDrawItem::Type active_type = SpriteTextDrawItem::Text;
            bool has_active_pipeline = false;
            for (const SpriteTextDrawItem& item : draw_items)
            {
                if (!has_active_pipeline || active_type != item.type)
                {
                    active_type = item.type;
                    has_active_pipeline = true;
                    command_list.SetGraphicsPipeline(item.type == SpriteTextDrawItem::Sprite ? *sprite_pipeline : *text_pipeline);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);
                }

                if (item.type == SpriteTextDrawItem::Sprite)
                {
                    const Scene::RenderData::Sprite3DRenderable& renderable = render_data.sprite_3d_renderables[item.index];
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
                    command_list.PushConstants(RHIShaderStage::Vertex, &push_constants, sizeof(SpritePushConstants), 0);
                    command_list.Draw(6, 1, 0, 0);
                }
                else
                {
                    const Scene::RenderData::Text3DRenderable& renderable = render_data.text_3d_renderables[item.index];
                    if (!renderable.font || !utils::CreateRenderData(*device, *renderable.font) || !renderable.font->render_data.IsValid())
                    {
                        continue;
                    }
                    if (renderable.size.x <= 0.0f || renderable.size.y <= 0.0f)
                    {
                        continue;
                    }

                    SpritePushConstants sprite_push_constants = {};
                    sprite_push_constants.Init();
                    sprite_push_constants.size_pivot = { renderable.size.x, renderable.size.y, -renderable.position.x / renderable.size.x, -renderable.position.y / renderable.size.y };
                    sprite_push_constants.uv_rect = renderable.uv_rect;
                    sprite_push_constants.instance_index = renderable.instance_index;
                    if (renderable.IsBillboard())
                    {
                        sprite_push_constants.flags |= SHADER_SPRITE_FLAG_BILLBOARD;
                    }
                    sprite_push_constants.material_index = renderable.material_index;
                    sprite_push_constants.SetResourceIndex(static_cast<uint32>(renderable.font->render_data.atlas_srv.descriptor_index));
                    command_list.PushConstants(RHIShaderStage::Vertex, &sprite_push_constants, sizeof(SpritePushConstants), 0);
                    command_list.Draw(6, 1, 0, 0);
                }
            }
        }

        if (pass == RenderPassType::Sprite2DPass && (flags & DrawScene_2DSprite) != 0 && (!render_data.sprite_2d_renderables.empty() || !render_data.text_2d_renderables.empty()))
        {
            struct Sprite2DDrawItem
            {
                enum Type : uint32
                {
                    Sprite,
                    Text
                };

                Type type = Sprite;
                Size index = 0;
                int32 layer = 0;
            };

            GraphicsPipelineHash sprite_2d_pipeline_hash = {};
            sprite_2d_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite2DPass);
            sprite_2d_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            sprite_2d_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            sprite_2d_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            sprite_2d_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
            sprite_2d_pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite2DPassMode::Sprite);

            GraphicsPipelineHash text_2d_pipeline_hash = sprite_2d_pipeline_hash;
            text_2d_pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite2DPassMode::Text);

            std::shared_ptr<RHIPipeline> sprite_2d_pipeline = shader_library.GetPipeline(sprite_2d_pipeline_hash);
            std::shared_ptr<RHIPipeline> text_2d_pipeline = shader_library.GetPipeline(text_2d_pipeline_hash);
            if (!sprite_2d_pipeline || !text_2d_pipeline)
            {
                return false;
            }

            Vector<Sprite2DDrawItem> draw_items;
            draw_items.reserve(render_data.sprite_2d_renderables.size() + render_data.text_2d_renderables.size());
            for (Size sprite_index = 0; sprite_index < render_data.sprite_2d_renderables.size(); ++sprite_index)
            {
                const Scene::RenderData::Sprite2DRenderable& renderable = render_data.sprite_2d_renderables[sprite_index];
                draw_items.push_back({ Sprite2DDrawItem::Sprite, sprite_index, renderable.layer });
            }
            for (Size text_index = 0; text_index < render_data.text_2d_renderables.size(); ++text_index)
            {
                const Scene::RenderData::Text2DRenderable& renderable = render_data.text_2d_renderables[text_index];
                draw_items.push_back({ Sprite2DDrawItem::Text, text_index, renderable.layer });
            }
            std::stable_sort(draw_items.begin(), draw_items.end(), [](const Sprite2DDrawItem& lhs, const Sprite2DDrawItem& rhs) {
                return lhs.layer < rhs.layer;
            });

            const float2 viewport_size = { static_cast<float>(view.viewport.width), static_cast<float>(view.viewport.height) };
            auto pack_sprite_2d_position = [&](const float2& anchor, const float2& position)
            {
                const float2 pixel_position = { anchor.x * viewport_size.x + position.x, anchor.y * viewport_size.y + position.y };
                const float normalized_x = viewport_size.x > 0.0f ? pixel_position.x / viewport_size.x : 0.0f;
                const float normalized_y = viewport_size.y > 0.0f ? pixel_position.y / viewport_size.y : 0.0f;
                return static_cast<uint32>(XMConvertFloatToHalf(normalized_x)) | (static_cast<uint32>(XMConvertFloatToHalf(normalized_y)) << 16);
            };

            command_list.SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            Sprite2DDrawItem::Type active_type = Sprite2DDrawItem::Text;
            bool has_active_pipeline = false;
            for (const Sprite2DDrawItem& item : draw_items)
            {
                if (!has_active_pipeline || active_type != item.type)
                {
                    active_type = item.type;
                    has_active_pipeline = true;
                    command_list.SetGraphicsPipeline(item.type == Sprite2DDrawItem::Sprite ? *sprite_2d_pipeline : *text_2d_pipeline);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                    command_list.SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);
                }

                if (item.type == Sprite2DDrawItem::Sprite)
                {
                    const Scene::RenderData::Sprite2DRenderable& renderable = render_data.sprite_2d_renderables[item.index];
                    SpritePushConstants push_constants = {};
                    push_constants.Init();
                    push_constants.size_pivot = { renderable.size.x, renderable.size.y, renderable.pivot.x, renderable.pivot.y };
                    push_constants.uv_rect = renderable.uv_rect;
                    push_constants.instance_index = pack_sprite_2d_position(renderable.anchor, renderable.position);
                    push_constants.material_index = renderable.material_index;
                    command_list.PushConstants(RHIShaderStage::Vertex, &push_constants, sizeof(SpritePushConstants), 0);
                    command_list.Draw(6, 1, 0, 0);
                }
                else
                {
                    const Scene::RenderData::Text2DRenderable& renderable = render_data.text_2d_renderables[item.index];
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
                    push_constants.size_pivot = { renderable.size.x, renderable.size.y, 0.0f, 0.0f };
                    push_constants.uv_rect = renderable.uv_rect;
                    push_constants.instance_index = pack_sprite_2d_position(renderable.anchor, renderable.position);
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
        clear_color = desc.clear_color;

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

        RHIBufferDesc shader_camera_buffer_desc = {};
        shader_camera_buffer_desc.size = sizeof(ShaderCamera);
        shader_camera_buffer_desc.usage = RHIResourceUsage::Default;
        shader_camera_buffer_desc.bind_flags = RHIBindFlags::ConstantBuffer;
        shader_camera_buffer = device->CreateBuffer(shader_camera_buffer_desc);
        if (!shader_camera_buffer)
        {
            backlog::Post("failed to create shader camera buffer", backlog::LogLevel::Error);
            return;
        }
        shader_camera_buffer->SetName("Shader Camera Buffer");

        RHISubresourceDesc shader_camera_subresource_desc = {};
        shader_camera_subresource_desc.type = RHISubresourceType::ConstantBuffer;
        shader_camera_subresource_desc.buffer_offset = 0;
        shader_camera_subresource_desc.buffer_size = sizeof(ShaderCamera);
        if (!device->CreateSubresource(*shader_camera_buffer, shader_camera_subresource_desc, &shader_camera_buffer_cbv))
        {
            backlog::Post("failed to create shader camera subresource", backlog::LogLevel::Error);
            shader_camera_buffer = nullptr;
            return;
        }

        current_frame_slot = 0;
    }

    void RendererInternal::BeginFrame(platform::Window& window)
    {
        if (current_window != &window)
        {
            WaitIdle();
            back_buffers_rtv = {};
            depth_buffer_dsv = {};
            depth_buffer = nullptr;
        }

        current_window = &window;
        const uint32 frame_slot = current_frame_slot;
        FrameContext& frame_context = GetFrameContext();

        // wait for fence here
        frame_context.BeginFrame();

        RHICommandList* command_list = frame_context.BeginCommandList(*device);
        profiler::BeginFrameGPU(*device, frame_slot, *command_list);

        device->BeginFrame(frame_slot);

        CreateRenderTargetResources(frame_context);

        RHISubresourceBinding back_buffer_binding = {};
        RHISubresourceBinding depth_buffer_binding = {};
        if (!GetCurrentBackBufferBinding(back_buffer_binding) || !GetCurrentDepthBufferBinding(depth_buffer_binding))
        {
            return;
        }

        command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::RenderTarget);
        command_list->TransitionResource(*depth_buffer_binding.resource, RHIResourceState::DepthWrite);
        command_list->ClearRenderTarget(back_buffer_binding, clear_color);
        command_list->ClearDepthStencil(depth_buffer_binding, 0.0f, 0u);
    }

    void RendererInternal::OnResize(platform::Window& window, uint32 width, uint32 height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }

        std::shared_ptr<RHISwapchain> swapchain = window.GetRHISwapchain();
        if (!swapchain)
        {
            return;
        }

        WaitIdle();

        back_buffers_rtv = {};
        depth_buffer_dsv = {};
        depth_buffer = nullptr;

        if (!swapchain->Resize(width, height))
        {
            backlog::Post("failed to resize swapchain", backlog::LogLevel::Error);
        }
    }

    void RendererInternal::UpdateDDGIProbe(FrameContext& frame_context, const ShaderEnvironmentLighting& environment_lighting, const ShaderDDGIVolume& ddgi_volume, const RHISubresourceBinding& shader_frame_binding, const RHISubresourceBinding& shader_camera_binding, RHICommandList& command_list)
    {
        resource::ShaderLibrary& shader_library = GetShaderLibrary();
        std::shared_ptr<RHIShader> current_ddgi_probe_update_shader = shader_library.GetShader(ShaderId::CSDDGIProbeUpdate);
        if (ddgi_probe_update_shader != current_ddgi_probe_update_shader)
        {
            ddgi_probe_update_pipeline = nullptr;
            ddgi_probe_update_shader = current_ddgi_probe_update_shader;
        }

        if (!ddgi_probe_update_pipeline && ddgi_probe_update_shader)
        {
            RHIComputePipelineDesc ddgi_probe_update_pipeline_desc = {};
            ddgi_probe_update_pipeline_desc.compute_shader = ddgi_probe_update_shader.get();
            ddgi_probe_update_pipeline = device->CreateComputePipeline(ddgi_probe_update_pipeline_desc);
            if (ddgi_probe_update_pipeline)
            {
                ddgi_probe_update_pipeline->SetName("DDGI Probe Update Pipeline");
            }
        }

        debug_state.ddgi.probe_update_pipeline_ready = ddgi_probe_update_pipeline != nullptr;

        const uint32 probe_update_start = ddgi_volume.total_probe_count > 0 ? ddgi_probe_update_offset % ddgi_volume.total_probe_count : 0;
        const uint32 probes_per_frame = ddgi_history_valid ? (std::min)(ddgi_volume.probes_per_frame, ddgi_volume.total_probe_count) : ddgi_volume.total_probe_count;
        const uint32 probe_update_dispatch_width = (std::min)(probes_per_frame, 65535u);

        if (environment_lighting.gi_mode != SHADER_ENVIRONMENT_GI_MODE_DDGI ||
            (ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) == 0 ||
            ddgi_volume.probe_counts.x == 0 ||
            ddgi_volume.probe_counts.y == 0 ||
            ddgi_volume.probe_counts.z == 0 ||
            ddgi_volume.total_probe_count == 0 ||
            probes_per_frame == 0 ||
            !ddgi_probe_update_pipeline ||
            !ddgi_irradiance_texture ||
            !ddgi_irradiance_history_texture ||
            !ddgi_visibility_texture ||
            !ddgi_visibility_history_texture ||
            !ddgi_probe_data_buffer ||
            !ddgi_probe_data_history_buffer ||
            !ddgi_irradiance_texture_srv.IsValid() ||
            !ddgi_irradiance_texture_uav.IsValid() ||
            !ddgi_irradiance_history_texture_srv.IsValid() ||
            !ddgi_visibility_texture_srv.IsValid() ||
            !ddgi_visibility_texture_uav.IsValid() ||
            !ddgi_visibility_history_texture_srv.IsValid() ||
            !ddgi_probe_data_buffer_srv.IsValid() ||
            !ddgi_probe_data_buffer_uav.IsValid() ||
            !ddgi_probe_data_history_buffer_srv.IsValid())
        {
            return;
        }

        const uint32 dispatch_width = (std::max)(probe_update_dispatch_width, 1u);
        debug_state.ddgi.dispatch_groups = {
            dispatch_width,
            (probes_per_frame + dispatch_width - 1) / dispatch_width,
            1
        };

        auto gpu_range = profiler::ScopedRangeGPU("DDGI Probe Update", command_list);
        command_list.BeginEvent("DDGI Probe Update");
        command_list.TransitionResource(*ddgi_irradiance_texture, RHIResourceState::ShaderWrite);
        command_list.TransitionResource(*ddgi_visibility_texture, RHIResourceState::ShaderWrite);
        command_list.TransitionResource(*ddgi_probe_data_buffer, RHIResourceState::ShaderWrite);
        if (ddgi_history_valid)
        {
            command_list.TransitionResource(*ddgi_irradiance_history_texture, RHIResourceState::ShaderRead);
            command_list.TransitionResource(*ddgi_visibility_history_texture, RHIResourceState::ShaderRead);
            command_list.TransitionResource(*ddgi_probe_data_history_buffer, RHIResourceState::ShaderRead);
        }

        command_list.SetComputePipeline(*ddgi_probe_update_pipeline);
        command_list.SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);
        command_list.SetConstantBuffer(RHIShaderStage::Compute, 1, shader_camera_binding);
        command_list.Dispatch(debug_state.ddgi.dispatch_groups.x, debug_state.ddgi.dispatch_groups.y, debug_state.ddgi.dispatch_groups.z);

        command_list.UAVBarrier(*ddgi_irradiance_texture);
        command_list.UAVBarrier(*ddgi_visibility_texture);
        command_list.UAVBarrier(*ddgi_probe_data_buffer);

        command_list.TransitionResource(*ddgi_irradiance_texture, RHIResourceState::CopySource);
        command_list.TransitionResource(*ddgi_visibility_texture, RHIResourceState::CopySource);
        command_list.TransitionResource(*ddgi_probe_data_buffer, RHIResourceState::CopySource);
        command_list.TransitionResource(*ddgi_irradiance_history_texture, RHIResourceState::CopyDest);
        command_list.TransitionResource(*ddgi_visibility_history_texture, RHIResourceState::CopyDest);
        command_list.TransitionResource(*ddgi_probe_data_history_buffer, RHIResourceState::CopyDest);

        command_list.CopyResource(*ddgi_irradiance_history_texture, *ddgi_irradiance_texture);
        command_list.CopyResource(*ddgi_visibility_history_texture, *ddgi_visibility_texture);
        command_list.CopyResource(*ddgi_probe_data_history_buffer, *ddgi_probe_data_buffer);
        if (debug_options.ddgi_debug_enable && debug_state.ddgi.probe_data_readback_buffer)
        {
            command_list.CopyBuffer(*debug_state.ddgi.probe_data_readback_buffer, 0, *ddgi_probe_data_buffer, 0, ddgi_probe_data_buffer->GetDesc().buffer_desc.size);
        }

        command_list.TransitionResource(*ddgi_irradiance_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*ddgi_visibility_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*ddgi_probe_data_buffer, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*ddgi_irradiance_history_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*ddgi_visibility_history_texture, RHIResourceState::ShaderRead);
        command_list.TransitionResource(*ddgi_probe_data_history_buffer, RHIResourceState::ShaderRead);
        command_list.EndEvent();

        debug_state.ddgi.probe_update_dispatched = true;
        ddgi_probe_update_offset = (probe_update_start + probes_per_frame) % ddgi_volume.total_probe_count;
        ddgi_history_valid = true;
        debug_state.ddgi.probe_data_readback_valid = debug_options.ddgi_debug_enable && debug_state.ddgi.probe_data_readback_buffer != nullptr;
    }

    void RendererInternal::UpdateDebugState(const View& view, const Scene::RenderData& render_data)
    {
        std::shared_ptr<RHIResource> ddgi_probe_data_readback_buffer = debug_state.ddgi.probe_data_readback_buffer;
        const bool ddgi_probe_data_readback_valid = debug_state.ddgi.probe_data_readback_valid;
        debug_state.ddgi = {};
        debug_state.ddgi.probe_data_readback_buffer = ddgi_probe_data_readback_buffer;
        debug_state.ddgi.probe_data_readback_valid = ddgi_probe_data_readback_valid;
        if (debug_options.bvh_debug_enable)
        {
            debug_state.bvh = {};
            const math::bvh::BVH& cpu_bvh = view.scene->GetSceneBVH();
            debug_state.bvh.cpu_bvh_available = cpu_bvh.IsValid();
            debug_state.bvh.cpu_nodes.reserve(cpu_bvh.nodes.size());
            for (const math::bvh::BVHNode& node : cpu_bvh.nodes)
            {
                RendererDebugBVHState::BVHNode debug_node = {};
                debug_node.bounds_min = node.bounds.min;
                debug_node.bounds_max = node.bounds.max;
                debug_node.is_leaf = node.IsLeaf();
                debug_state.bvh.cpu_nodes.push_back(debug_node);
            }
        }
        else
        {
            debug_state.bvh = {};
        }
        debug_state.ddgi.gi_mode_ddgi = render_data.shader_environment_lighting.gi_mode == SHADER_ENVIRONMENT_GI_MODE_DDGI;
        debug_state.ddgi.volume_active = (render_data.shader_ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) != 0;
        if (debug_state.ddgi.volume_active)
        {
            const uint3& probe_counts = render_data.shader_ddgi_volume.probe_counts;
            const float3& probe_spacing = render_data.shader_ddgi_volume.probe_spacing;
            const float3 probe_span = {
                static_cast<float>((probe_counts.x > 0 ? probe_counts.x - 1 : 0)) * probe_spacing.x,
                static_cast<float>((probe_counts.y > 0 ? probe_counts.y - 1 : 0)) * probe_spacing.y,
                static_cast<float>((probe_counts.z > 0 ? probe_counts.z - 1 : 0)) * probe_spacing.z
            };

            debug_state.ddgi.volume_entity = render_data.ddgi_volume_entity;
            debug_state.ddgi.probe_counts = probe_counts;
            debug_state.ddgi.volume_min = render_data.shader_ddgi_volume.volume_min;
            debug_state.ddgi.volume_max = {
                render_data.shader_ddgi_volume.volume_min.x + probe_span.x,
                render_data.shader_ddgi_volume.volume_min.y + probe_span.y,
                render_data.shader_ddgi_volume.volume_min.z + probe_span.z
            };
            debug_state.ddgi.probe_spacing = probe_spacing;
            debug_state.ddgi.total_probe_count = render_data.shader_ddgi_volume.total_probe_count;
        }
        debug_state.ddgi.irradiance_texture_allocated = ddgi_irradiance_texture != nullptr;
        debug_state.ddgi.irradiance_srv_valid = ddgi_irradiance_texture_srv.IsValid();
        debug_state.ddgi.irradiance_uav_valid = ddgi_irradiance_texture_uav.IsValid();
        debug_state.ddgi.visibility_texture_allocated = ddgi_visibility_texture != nullptr;
        debug_state.ddgi.visibility_srv_valid = ddgi_visibility_texture_srv.IsValid();
        debug_state.ddgi.visibility_uav_valid = ddgi_visibility_texture_uav.IsValid();
        debug_state.ddgi.probe_data_buffer_allocated = ddgi_probe_data_buffer != nullptr;
        debug_state.ddgi.probe_data_srv_valid = ddgi_probe_data_buffer_srv.IsValid();
        debug_state.ddgi.probe_data_uav_valid = ddgi_probe_data_buffer_uav.IsValid();
        debug_state.ddgi.history_valid = ddgi_history_valid;
        debug_state.ddgi.irradiance_texture_srv = ddgi_irradiance_texture_srv.descriptor_index;
        debug_state.ddgi.irradiance_texture_uav = ddgi_irradiance_texture_uav.descriptor_index;
        debug_state.ddgi.visibility_texture_srv = ddgi_visibility_texture_srv.descriptor_index;
        debug_state.ddgi.visibility_texture_uav = ddgi_visibility_texture_uav.descriptor_index;
        debug_state.ddgi.probe_data_buffer_srv = ddgi_probe_data_buffer_srv.descriptor_index;
        debug_state.ddgi.probe_data_buffer_uav = ddgi_probe_data_buffer_uav.descriptor_index;
        if (debug_options.ddgi_debug_enable && debug_state.ddgi.gi_mode_ddgi && debug_state.ddgi.volume_active && debug_state.ddgi.probe_data_readback_valid && debug_state.ddgi.probe_data_readback_buffer && debug_state.ddgi.probe_data_readback_buffer->GetMappedData())
        {
            const uint32 max_debug_probe_count = 4096;
            const uint32 total_probe_count = render_data.shader_ddgi_volume.total_probe_count;
            const float sample_ratio = total_probe_count > max_debug_probe_count ? static_cast<float>(total_probe_count) / static_cast<float>(max_debug_probe_count) : 1.0f;
            const uint32 sampling_step = sample_ratio > 1.0f ? static_cast<uint32>((std::max)(1.0f, std::ceil(std::cbrt(sample_ratio)))) : 1u;
            const float4* probe_data = static_cast<const float4*>(debug_state.ddgi.probe_data_readback_buffer->GetMappedData());
            const Size readback_probe_count = debug_state.ddgi.probe_data_readback_buffer->GetDesc().buffer_desc.size / sizeof(float4);
            debug_state.ddgi.probes.reserve((std::min)(total_probe_count, max_debug_probe_count));

            for (uint32 z = 0; z < render_data.shader_ddgi_volume.probe_counts.z; z += sampling_step)
            {
                for (uint32 y = 0; y < render_data.shader_ddgi_volume.probe_counts.y; y += sampling_step)
                {
                    for (uint32 x = 0; x < render_data.shader_ddgi_volume.probe_counts.x; x += sampling_step)
                    {
                        const uint32 probe_linear_index = x + y * render_data.shader_ddgi_volume.probe_counts.x + z * render_data.shader_ddgi_volume.probe_counts.x * render_data.shader_ddgi_volume.probe_counts.y;
                        if (probe_linear_index >= readback_probe_count)
                        {
                            continue;
                        }

                        const float4 data = probe_data[probe_linear_index];
                        RendererDebugDDGIState::DDGIProbe debug_probe = {};
                        debug_probe.position = {
                            render_data.shader_ddgi_volume.volume_min.x + static_cast<float>(x) * render_data.shader_ddgi_volume.probe_spacing.x + data.x,
                            render_data.shader_ddgi_volume.volume_min.y + static_cast<float>(y) * render_data.shader_ddgi_volume.probe_spacing.y + data.y,
                            render_data.shader_ddgi_volume.volume_min.z + static_cast<float>(z) * render_data.shader_ddgi_volume.probe_spacing.z + data.z
                        };
                        debug_probe.relocation = std::sqrt(data.x * data.x + data.y * data.y + data.z * data.z);
                        debug_probe.validity = data.w;
                        debug_state.ddgi.probes.push_back(debug_probe);
                    }
                }
            }
        }
    }

    void RendererInternal::Render(const View& view)
    {
        switch (view.render_path_type)
        {
        case RenderPathType::Forward:
        default:
            RenderForwardPath(view);
            break;
        }
    }

    void RendererInternal::RenderForwardPath(const View& view)
    {
        auto render_cpu_range = profiler::ScopedRangeCPU("RendererInternal::RenderForwardPath");

        if (!view.scene || view.camera_entity == ecs::INVALID_ENTITY || !current_window)
            return;

        FrameContext& frame_context = GetFrameContext();

        const Scene::RenderData render_data = view.scene->GetRenderData();

        {
            auto cpu_range = profiler::ScopedRangeCPU("Create Render Resources");
            if (!CreateShadowMapAtlasResources(frame_context, render_data) ||
                !CreateDDGIResources(frame_context, render_data.shader_ddgi_volume))
            {
                return;
            }
        }

        {
            auto cpu_range = profiler::ScopedRangeCPU("Build Shadow Cascades");
            BuildShadowCascades(view);
        }
        {
            auto cpu_range = profiler::ScopedRangeCPU("Update Debug State");
            UpdateDebugState(view, render_data);
        }

        view.scene->BuildGPUBVH();
        if (enqueued_work_fence_value > 0 && enqueued_work_fence->GetCompletedValue() >= enqueued_work_fence_value)
        {
            enqueued_work_fence_value = 0;
            enqueued_work_scratch_resources.clear();
        }
        if (enqueued_work_fence_value == 0)
        {
            enqueued_work_scratch_resources.clear();
            enqueued_work_succeeded = true;
            jobsystem::Execute(GetRenderingWorkContext(), [this](jobsystem::JobArgs args) {
                enqueued_work_command_allocator->Reset();
                enqueued_work_command_list->Begin(*enqueued_work_command_allocator);
                enqueued_work_succeeded = utils::FlushEnqueuedRenderingWork(*device, *enqueued_work_command_list, enqueued_work_scratch_resources);
            });
        }

        RHICommandList* command_list = frame_context.BeginCommandList(*device);
        if (!command_list)
        {
            return;
        }

        jobsystem::Execute(GetRenderingWorkContext(), [this, view, command_list](jobsystem::JobArgs args) {
            FrameContext& frame_context = GetFrameContext();
            const Scene::RenderData render_data = view.scene->GetRenderData();

            {
                auto cpu_range = profiler::ScopedRangeCPU("Update Scene GPU Data");
                auto gpu_range = profiler::ScopedRangeGPU("Update Scene GPU Data", *command_list);
                if (!UpdateSceneGPUData(frame_context, render_data, *command_list))
                {
                    return;
                }
            }

            {
                auto cpu_range = profiler::ScopedRangeCPU("Update Frame Constants");
                auto gpu_range = profiler::ScopedRangeGPU("Update Frame Constants", *command_list);
                if (!UpdateFrameConstants(frame_context, view, render_data, *command_list))
                {
                    return;
                }
            }


            RHISubresourceBinding back_buffer_binding = {};
            RHISubresourceBinding depth_buffer_binding = {};
            if (!GetCurrentBackBufferBinding(back_buffer_binding) || !GetCurrentDepthBufferBinding(depth_buffer_binding))
            {
                return;
            }

            Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };
            RHISubresourceBinding shader_frame_binding = {};
            shader_frame_binding.resource = shader_frame_buffer.get();
            shader_frame_binding.subresource = shader_frame_buffer_cbv;
            RHISubresourceBinding shader_camera_binding = {};
            shader_camera_binding.resource = shader_camera_buffer.get();
            shader_camera_binding.subresource = shader_camera_buffer_cbv;

            RHIViewport viewport = {};
            viewport.x = static_cast<float>(view.viewport.x);
            viewport.y = static_cast<float>(view.viewport.y);
            viewport.width = static_cast<float>(view.viewport.width);
            viewport.height = static_cast<float>(view.viewport.height);
            viewport.min_depth = 0.0f;
            viewport.max_depth = 1.0f;

            RHIRect scissor = {};
            scissor.x = view.scissor.x;
            scissor.y = view.scissor.y;
            scissor.width = view.scissor.width;
            scissor.height = view.scissor.height;

            {
                auto cpu_range = profiler::ScopedRangeCPU("DDGI Probe Update");
                UpdateDDGIProbe(frame_context, render_data.shader_environment_lighting, render_data.shader_ddgi_volume, shader_frame_binding, shader_camera_binding, *command_list);
            }

            command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::RenderTarget);
            command_list->TransitionResource(*depth_buffer_binding.resource, RHIResourceState::DepthWrite);
            command_list->SetViewport(viewport);
            command_list->SetScissor(scissor);

            if ((render_data.shader_sky.flags & SHADER_SKY_FLAG_ACTIVE) != 0)
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sky Pass", *command_list);
                command_list->BeginEvent("Sky Pass");
                command_list->SetRenderTargets(color_targets, nullptr);
                resource::ShaderLibrary& shader_library = GetShaderLibrary();
                GraphicsPipelineHash sky_pipeline_hash = {};
                sky_pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::SkyPass);
                sky_pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                sky_pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
                sky_pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                sky_pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                std::shared_ptr<RHIPipeline> sky_pipeline = shader_library.GetPipeline(sky_pipeline_hash);
                if (!sky_pipeline)
                {
                    command_list->EndEvent();
                    return;
                }
                command_list->SetGraphicsPipeline(*sky_pipeline);
                command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
                command_list->SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);
                command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
                command_list->Draw(3, 1, 0, 0);
                command_list->EndEvent();
            }

            if (shadow_map_atlas && shadow_map_atlas_dsv.IsValid() && !render_data.render_shadow_slices.empty())
            {
                auto cpu_range = profiler::ScopedRangeCPU("Shadow Pass");
                auto gpu_range = profiler::ScopedRangeGPU("Shadow Pass", *command_list);
                command_list->BeginEvent("Fill Shadow Map Atlas");

                RHISubresourceBinding shadow_map_atlas_binding = {};
                shadow_map_atlas_binding.resource = shadow_map_atlas.get();
                shadow_map_atlas_binding.subresource = shadow_map_atlas_dsv;

                command_list->TransitionResource(*shadow_map_atlas, RHIResourceState::DepthWrite);
                command_list->ClearDepthStencil(shadow_map_atlas_binding, 0.0f, 0u);
                command_list->SetRenderTargets({}, &shadow_map_atlas_binding);

                for (const auto& shadow_slice : render_data.render_shadow_slices)
                {
                    if (!shadow_slice.HasShadowMapAtlasRect())
                    {
                        continue;
                    }

                    ShaderCamera shadow_camera = {};
                    shadow_camera.Init();

                    shadow_camera.view_projection = shadow_slice.view_projection;

                    if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shadow_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer, 0, *command_list))
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

                    DrawScene(frame_context, view, RenderPassType::ShadowPass, DrawScene_Opaque, *command_list);
                }
                command_list->TransitionResource(*shadow_map_atlas, RHIResourceState::ShaderRead);
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
                        shader_camera.view = camera_component->view;
                        shader_camera.projection = camera_component->projection;
                        shader_camera.view_projection = camera_component->view_projection;
                        shader_camera.inv_view_projection = camera_component->inv_view_projection;
                    }
                }

                if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shader_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer, 0, *command_list))
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
                    auto cpu_range = profiler::ScopedRangeCPU("Draw Prepass");
                    DrawScene(frame_context, view, RenderPassType::DepthPrepass, DrawScene_Opaque, *command_list);
                }
                command_list->EndEvent();
            }
        
            // main pass
            {
                auto gpu_range = profiler::ScopedRangeGPU("Main Pass", *command_list);
                command_list->BeginEvent("Main Pass");

                command_list->SetRenderTargets(color_targets, &depth_buffer_binding);
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Draw Main Pass");
                    DrawScene(frame_context, view, RenderPassType::MainPass, DrawScene_Opaque | DrawScene_Transparent | DrawScene_Primitive, *command_list);
                }
                command_list->EndEvent();
            }

            // sprite/text 3d pass
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sprite/Text3D Pass", *command_list);
                command_list->BeginEvent("Sprite/Text3D Pass");

                command_list->SetRenderTargets(color_targets, &depth_buffer_binding);
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Draw Sprite/Text3D Pass");
                    DrawScene(frame_context, view, RenderPassType::Sprite3DPass, DrawScene_3DSprite, *command_list);
                }
                command_list->EndEvent();
            }

            // sprite 2d pass
            {
                auto gpu_range = profiler::ScopedRangeGPU("Sprite2D Pass", *command_list);
                command_list->BeginEvent("Sprite2D Pass");

                command_list->SetRenderTargets(color_targets, nullptr);
                {
                    auto cpu_range = profiler::ScopedRangeCPU("Draw Sprite2D Pass");
                    DrawScene(frame_context, view, RenderPassType::Sprite2DPass, DrawScene_2DSprite, *command_list);
                }
                command_list->EndEvent();
            }
        });
    }

    void RendererInternal::EndFrame()
    {
        FrameContext& frame_context = GetFrameContext();

        std::shared_ptr<RHISwapchain> swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            return;
        }

        RHISubresourceBinding back_buffer_binding = {};
        if (!GetCurrentBackBufferBinding(back_buffer_binding))
        {
            return;
        }

        jobsystem::Wait(GetRenderingWorkContext());
        RHICommandList* final_command_list = frame_context.BeginCommandList(*device);

        profiler::EndFrameGPU(*final_command_list);
        final_command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::Present);

        const std::shared_ptr<RHIContext> graphics_context = device->GetContext(RHIQueueType::Graphics);
        frame_context.SubmitCommandLists(*graphics_context);

        if (!swapchain->Present())
        {
            backlog::Post("failed to present swapchain", backlog::LogLevel::Error);
            return;
        }

        ++frame_count;
        current_frame_slot = (current_frame_slot + 1) % static_cast<uint32>(frame_contexts.size());

        if (enqueued_work_fence_value == 0)
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
            frame_context.shader_instance_upload_buffer = nullptr;
            frame_context.shader_geometry_upload_buffer = nullptr;
            frame_context.shader_material_upload_buffer = nullptr;
            frame_context.shader_light_upload_buffer = nullptr;
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
        shader_instance_default_buffer_srv = {};
        shader_instance_default_buffer = nullptr;
        shader_geometry_default_buffer_srv = {};
        shader_geometry_default_buffer = nullptr;
        shader_material_default_buffer_srv = {};
        shader_material_default_buffer = nullptr;
        shader_bone_matrix_default_buffer_srv = {};
        shader_bone_matrix_default_buffer = nullptr;
        shader_light_default_buffer_srv = {};
        shader_light_default_buffer = nullptr;
        shader_shadow_cascade_default_buffer_srv = {};
        shader_shadow_cascade_default_buffer = nullptr;
        shader_bvh_node_default_buffer_srv = {};
        shader_bvh_node_default_buffer = nullptr;
        shader_bvh_instance_default_buffer_srv = {};
        shader_bvh_instance_default_buffer = nullptr;
        shader_frame_buffer_cbv = {};
        shader_frame_buffer = nullptr;
        shader_camera_buffer_cbv = {};
        shader_camera_buffer = nullptr;
        shadow_map_atlas_dsv = {};
        shadow_map_atlas_srv = {};
        shadow_map_atlas = nullptr;
        shadow_map_atlas_size = { 0, 0 };
        ddgi_irradiance_texture_uav = {};
        ddgi_irradiance_texture_srv = {};
        ddgi_irradiance_texture = nullptr;
        ddgi_irradiance_history_texture_srv = {};
        ddgi_irradiance_history_texture = nullptr;
        ddgi_visibility_texture_uav = {};
        ddgi_visibility_texture_srv = {};
        ddgi_visibility_texture = nullptr;
        ddgi_visibility_history_texture_srv = {};
        ddgi_visibility_history_texture = nullptr;
        ddgi_probe_data_buffer_uav = {};
        ddgi_probe_data_buffer_srv = {};
        ddgi_probe_data_buffer = nullptr;
        ddgi_probe_data_history_buffer_srv = {};
        ddgi_probe_data_history_buffer = nullptr;
        debug_state.ddgi.probe_data_readback_buffer = nullptr;
        debug_state.ddgi.probe_data_readback_valid = false;
        ddgi_probe_update_pipeline = nullptr;
        ddgi_probe_update_shader = nullptr;
        debug_state = {};
        ddgi_probe_counts = { 0, 0, 0 };
        ddgi_probe_spacing = { 0.0f, 0.0f, 0.0f };
        ddgi_volume_min = { 0.0f, 0.0f, 0.0f };
        ddgi_max_distance = 0.0f;
        ddgi_probe_update_offset = 0;
        ddgi_history_valid = false;
        back_buffers_rtv = {};
        depth_buffer_dsv = {};
        depth_buffer = nullptr;
        GetShaderLibrary().ClearAll();
        device.reset();
    }
}
