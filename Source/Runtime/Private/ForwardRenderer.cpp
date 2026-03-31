#include "ForwardRenderer.h"

#include "Backlog.h"
#include "Scene.h"
#include "MathUtils.h"

#include "Window.h"
#include "Entity.h"
#include "CameraComponent.h"

#include <cstring>

using namespace won::resource;
using namespace won::ecs;
using namespace won::math;

namespace won::rendering
{
    bool ForwardRenderer::AllocateFrameUpload(FrameContext& frame_context, Size size, Size alignment, FrameUploadAllocation& out_allocation)
    {
        Size aligned_offset = 0;
        if (!frame_context.frame_upload_buffer)
        {
            RHIBufferDesc frame_upload_buffer_desc = {};
            frame_upload_buffer_desc.size = align((Size)1024 * 20, alignment); // initial_size
            frame_upload_buffer_desc.usage = RHIResourceUsage::Upload;
            frame_upload_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::VertexBuffer | RHIBindFlags::IndexBuffer;
            frame_context.frame_upload_buffer = device->CreateBuffer(frame_upload_buffer_desc);
            frame_context.frame_upload_offset = 0;
        }

        Size buffer_size = frame_context.frame_upload_buffer->GetDesc().buffer_desc.size;
        aligned_offset = align(frame_context.frame_upload_offset, alignment);
        Size required_size = aligned_offset + size;
        
        if (buffer_size < required_size)
        {
            RHIBufferDesc new_desc = frame_context.frame_upload_buffer->GetDesc().buffer_desc;
            new_desc.size = align(required_size * 2, alignment);
            frame_context.deferred_res_removal.push_back(frame_context.frame_upload_buffer);
            frame_context.frame_upload_buffer = device->CreateBuffer(new_desc);
        }
        frame_context.frame_upload_offset = aligned_offset + size;

        void* mapped_data = frame_context.frame_upload_buffer->GetMappedData();

        out_allocation.mapped_data = static_cast<uint8*>(mapped_data) + aligned_offset;
        out_allocation.buffer_offset = aligned_offset;

        return true;
    }

    bool ForwardRenderer::UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset)
    {
        const RHIResourceDesc& destination_desc = destination_buffer.GetDesc();
        Size upload_alignment = device->GetMinOffsetAlignment(destination_desc.buffer_desc);

        FrameUploadAllocation upload_allocation = {};
        if (!AllocateFrameUpload(frame_context, data_size, upload_alignment, upload_allocation))
        {
            return false;
        }

        std::memcpy(upload_allocation.mapped_data, source_data, data_size);

        frame_context.command_list->TransitionResource(destination_buffer, RHIResourceState::CopyDest);
        frame_context.command_list->CopyBuffer(destination_buffer, destination_offset, *frame_context.frame_upload_buffer, upload_allocation.buffer_offset, data_size);
        frame_context.command_list->TransitionResource(destination_buffer, final_state);
        return true;
    }

    bool ForwardRenderer::BuildFrameContext(const View& view, FrameContext& frame_context)
    {
        const Scene::RenderData& render_data = view.scene->GetRenderData();
        const Vector<ShaderInstance>& shader_instance = render_data.shader_instance;
        const Vector<ShaderGeometry>& shader_geometry = render_data.shader_geometry;
        const Vector<ShaderMaterial>& shader_material = render_data.shader_material;
        const Vector<ShaderLight>& shader_light = render_data.shader_light;

        const Size required_instance_buffer_size = shader_instance.size() * sizeof(ShaderInstance);
        const Size required_geometry_buffer_size = shader_geometry.size() * sizeof(ShaderGeometry);
        const Size required_material_buffer_size = shader_material.size() * sizeof(ShaderMaterial);
        const Size required_light_buffer_size = shader_light.size() * sizeof(ShaderLight);

        if (required_instance_buffer_size == 0)
        {
            frame_context.shader_instance_upload_buffer = nullptr;
            shader_instance_default_buffer = nullptr;
            shader_instance_default_buffer_subresource = {};
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

                shader_instance_default_buffer_subresource = {};
                RHISubresourceDesc shader_instance_default_subresource_desc = {};
                shader_instance_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_instance_default_subresource_desc.buffer_offset = 0;
                shader_instance_default_subresource_desc.buffer_size = shader_instance_default_buffer->GetDesc().buffer_desc.size;
                shader_instance_default_subresource_desc.buffer_stride = sizeof(ShaderInstance);
                if (!device->CreateSubresource(*shader_instance_default_buffer, shader_instance_default_subresource_desc, &shader_instance_default_buffer_subresource))
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
            }

            void* mapped_data = frame_context.shader_instance_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped instance upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_instance.data(), required_instance_buffer_size);

            frame_context.command_list->TransitionResource(*shader_instance_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_instance_default_buffer, *frame_context.shader_instance_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_instance_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_geometry_buffer_size == 0)
        {
            frame_context.shader_geometry_upload_buffer = nullptr;
            shader_geometry_default_buffer = nullptr;
            shader_geometry_default_buffer_subresource = {};
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

                shader_geometry_default_buffer_subresource = {};
                RHISubresourceDesc shader_geometry_default_subresource_desc = {};
                shader_geometry_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_geometry_default_subresource_desc.buffer_offset = 0;
                shader_geometry_default_subresource_desc.buffer_size = shader_geometry_default_buffer->GetDesc().buffer_desc.size;
                shader_geometry_default_subresource_desc.buffer_stride = sizeof(ShaderGeometry);
                if (!device->CreateSubresource(*shader_geometry_default_buffer, shader_geometry_default_subresource_desc, &shader_geometry_default_buffer_subresource))
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
            }

            void* mapped_data = frame_context.shader_geometry_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped geometry upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_geometry.data(), required_geometry_buffer_size);

            frame_context.command_list->TransitionResource(*shader_geometry_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_geometry_default_buffer, *frame_context.shader_geometry_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_geometry_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_material_buffer_size == 0)
        {
            frame_context.shader_material_upload_buffer = nullptr;
            shader_material_default_buffer = nullptr;
            shader_material_default_buffer_subresource = {};
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

                shader_material_default_buffer_subresource = {};
                RHISubresourceDesc shader_material_default_subresource_desc = {};
                shader_material_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_material_default_subresource_desc.buffer_offset = 0;
                shader_material_default_subresource_desc.buffer_size = shader_material_default_buffer->GetDesc().buffer_desc.size;
                shader_material_default_subresource_desc.buffer_stride = sizeof(ShaderMaterial);
                if (!device->CreateSubresource(*shader_material_default_buffer, shader_material_default_subresource_desc, &shader_material_default_buffer_subresource))
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
            }

            void* mapped_data = frame_context.shader_material_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped material upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_material.data(), required_material_buffer_size);

            frame_context.command_list->TransitionResource(*shader_material_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_material_default_buffer, *frame_context.shader_material_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_material_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_light_buffer_size == 0)
        {
            frame_context.shader_light_upload_buffer = nullptr;
            shader_light_default_buffer = nullptr;
            shader_light_default_buffer_subresource = {};
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

                shader_light_default_buffer_subresource = {};
                RHISubresourceDesc shader_light_default_subresource_desc = {};
                shader_light_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_light_default_subresource_desc.buffer_offset = 0;
                shader_light_default_subresource_desc.buffer_size = shader_light_default_buffer->GetDesc().buffer_desc.size;
                shader_light_default_subresource_desc.buffer_stride = sizeof(ShaderLight);
                if (!device->CreateSubresource(*shader_light_default_buffer, shader_light_default_subresource_desc, &shader_light_default_buffer_subresource))
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
            }

            void* mapped_data = frame_context.shader_light_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped light upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_light.data(), required_light_buffer_size);

            frame_context.command_list->TransitionResource(*shader_light_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_light_default_buffer, *frame_context.shader_light_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_light_default_buffer, RHIResourceState::ShaderRead);
        }

        ShaderFrame shader_frame{};
        shader_frame.Init();
        shader_frame.scene.instancebuffer = shader_instance_default_buffer_subresource.descriptor_index;
        shader_frame.scene.geometrybuffer = shader_geometry_default_buffer_subresource.descriptor_index;
        shader_frame.scene.materialbuffer = shader_material_default_buffer_subresource.descriptor_index;
        shader_frame.scene.lightbuffer = shader_light_default_buffer_subresource.descriptor_index;
        shader_frame.scene.lights = render_data.forward_light_mask;

        ShaderCamera shader_camera{};
        shader_camera.Init();
        shader_camera.view = IDENTITY_MATRIX;
        shader_camera.projection = IDENTITY_MATRIX;
        shader_camera.view_projection = IDENTITY_MATRIX;
        if (view.scene)
        {
            const ecs::CameraComponent* camera_component = nullptr;
            if (view.camera_entity != ecs::INVALID_ENTITY)
            {
                camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);

                shader_camera.position = camera_component->eye;
                shader_camera.forward = camera_component->forward;
                shader_camera.up = camera_component->up;
                shader_camera.z_near = camera_component->near;
                shader_camera.z_far = camera_component->far;
                shader_camera.view = camera_component->view;
                shader_camera.projection = camera_component->projection;
                shader_camera.view_projection = camera_component->view_projection;
            }
        }

        if (!UpdateDefaultBuffer(frame_context, *shader_frame_buffer, &shader_frame, sizeof(ShaderFrame), RHIResourceState::ConstantBuffer))
        {
            return false;
        }
        if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shader_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer))
        {
            return false;
        }

        return true;
    }

    bool ForwardRenderer::DrawScene(const View& view, const FrameContext& frame_context, RenderPassType pass, uint32 flags)
    {
        const Scene::RenderData& render_data = view.scene->GetRenderData();

        frame_context.command_list->SetGraphicsPipeline(*shader_library->GetPipeline(pass).get());

        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_subresource;
        frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);

        RHISubresourceBinding shader_camera_binding = {};
        shader_camera_binding.resource = shader_camera_buffer.get();
        shader_camera_binding.subresource = shader_camera_buffer_subresource;
        frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
        frame_context.command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

        for (const auto& renderable : render_data.renderables)
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

            if ((flags & DrawScene_ShadowCaster) != 0 && !renderable.IsCastShadow())
            {
                continue;
            }

            frame_context.command_list->SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_offset, renderable.index_count * sizeof(uint32));
            frame_context.command_list->PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
            frame_context.command_list->DrawIndexed(renderable.index_count, 1, 0, 0, 0);
        }

        return true;
    }

    void ForwardRenderer::Initialize(const RendererDesc& desc, std::shared_ptr<resource::ShaderLibrary> shader_lib)
    {
        device = desc.device;
        shader_library = shader_lib;

        frame_contexts = {};
        for (uint32 i = 0; i < max_frames_in_flight; ++i)
        {
            FrameContext& frame_context = frame_contexts[i];
            frame_context.command_allocator = device->CreateCommandAllocator(RHIQueueType::Graphics);
            frame_context.command_list = device->CreateCommandList(RHIQueueType::Graphics);
            frame_context.fence = device->CreateFence(0);
            if (!frame_context.command_allocator || !frame_context.command_list || !frame_context.fence)
            {
                backlog::Post("failed to create frame contexts", backlog::LogLevel::Error);
                frame_contexts = {};
                return;
            }
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

        RHISubresourceDesc shader_frame_subresource_desc = {};
        shader_frame_subresource_desc.type = RHISubresourceType::ConstantBuffer;
        shader_frame_subresource_desc.buffer_offset = 0;
        shader_frame_subresource_desc.buffer_size = sizeof(ShaderFrame);
        if (!device->CreateSubresource(*shader_frame_buffer, shader_frame_subresource_desc, &shader_frame_buffer_subresource))
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

        RHISubresourceDesc shader_camera_subresource_desc = {};
        shader_camera_subresource_desc.type = RHISubresourceType::ConstantBuffer;
        shader_camera_subresource_desc.buffer_offset = 0;
        shader_camera_subresource_desc.buffer_size = sizeof(ShaderCamera);
        if (!device->CreateSubresource(*shader_camera_buffer, shader_camera_subresource_desc, &shader_camera_buffer_subresource))
        {
            backlog::Post("failed to create shader camera subresource", backlog::LogLevel::Error);
            shader_camera_buffer = nullptr;
            return;
        }

        current_frame_slot = 0;
    }

    void ForwardRenderer::BeginFrame(platform::Window& window)
    {
        if (current_window != &window)
        {
            WaitIdle();
            back_buffer_subresources = {};
            depth_buffer_subresource = {};
            depth_buffer = nullptr;
        }

        current_window = &window;
    }

    void ForwardRenderer::OnResize(platform::Window& window, uint32 width, uint32 height)
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

        back_buffer_subresources = {};
        depth_buffer_subresource = {};
        depth_buffer = nullptr;

        if (!swapchain->Resize(width, height))
        {
            backlog::Post("failed to resize swapchain", backlog::LogLevel::Error);
        }
    }

    void ForwardRenderer::Render(const View& view)
    {
        if (!view.scene || !current_window)// || view.camera_entity == ecs::INVALID_ENTITY)
            return;

        std::shared_ptr<RHISwapchain> swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            swapchain = device->CreateSwapchain(*current_window);
            if (!swapchain)
            {
                backlog::Post("failed to create swapchain", backlog::LogLevel::Error);
                return;
            }
            current_window->SetRHISwapchain(swapchain);
        }

        std::shared_ptr<RHIResource> back_buffer = swapchain->GetCurrentBackBuffer();
        if (!back_buffer)
        {
            backlog::Post("failed to get swapchain back buffer", backlog::LogLevel::Error);
            return;
        }

        for (uint32 i = 0; i < swapchain->GetBackBufferCount() && i < static_cast<uint32>(back_buffer_subresources.size()); ++i)
        {
            if (back_buffer_subresources[i].IsValid())
            {
                continue;
            }

            std::shared_ptr<RHIResource> swapchain_back_buffer = swapchain->GetBackBuffer(i);
            if (!swapchain_back_buffer)
            {
                backlog::Post("failed to get swapchain back buffer for RTV creation", backlog::LogLevel::Error);
                return;
            }

            RHISubresourceDesc back_buffer_subresource_desc = {};
            back_buffer_subresource_desc.type = RHISubresourceType::RenderTarget;
            if (!device->CreateSubresource(*swapchain_back_buffer, back_buffer_subresource_desc, &back_buffer_subresources[i]))
            {
                backlog::Post("failed to create back buffer RTV", backlog::LogLevel::Error);
                return;
            }
        }

        const RHIResourceDesc& back_buffer_desc = back_buffer->GetDesc();
        const RHITextureDesc& back_buffer_texture_desc = back_buffer_desc.texture_desc;
        const uint32 target_sample_count = back_buffer_texture_desc.sample_count > 0 ? back_buffer_texture_desc.sample_count : 1;
        bool recreate_depth_buffer = !depth_buffer || !depth_buffer_subresource.IsValid();
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
                return;
            }

            depth_buffer_subresource = {};
            RHISubresourceDesc depth_subresource_desc = {};
            depth_subresource_desc.type = RHISubresourceType::DepthStencil;
            depth_subresource_desc.format = depth_desc.format;
            if (!device->CreateSubresource(*depth_buffer, depth_subresource_desc, &depth_buffer_subresource))
            {
                backlog::Post("failed to create depth buffer subresource", backlog::LogLevel::Error);
                depth_buffer = nullptr;
                return;
            }
        }

        FrameContext& frame_context = GetFrameContext();

        if (frame_context.fence_value > 0)
        {
            frame_context.fence->Wait(frame_context.fence_value);
            frame_context.fence_value = 0;
        }

        frame_context.deferred_res_removal.clear();
        frame_context.command_allocator->Reset();
        frame_context.command_list->Begin(*frame_context.command_allocator);
        frame_context.frame_upload_offset = 0;

        if (!BuildFrameContext(view, frame_context))
        {
            return;
        }

        const uint32 back_buffer_index = swapchain->GetCurrentBackBufferIndex();

        RHISubresourceBinding back_buffer_binding = {};
        back_buffer_binding.resource = back_buffer.get();
        back_buffer_binding.subresource = back_buffer_subresources[back_buffer_index];
        RHISubresourceBinding depth_buffer_binding = {};
        depth_buffer_binding.resource = depth_buffer.get();
        depth_buffer_binding.subresource = depth_buffer_subresource;
        Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };

        const Scene::RenderData& render_data = view.scene->GetRenderData();
        if (render_data.shadow_map_atlas_size.x == 0 || render_data.shadow_map_atlas_size.y == 0)
        {
            shadow_map_atlas = nullptr;
            shadow_map_atlas_subresource = {};
            shadow_map_atlas_size = { 0, 0 };
        }
        else
        {
            const bool regen_shadowmap_atlas =
                !shadow_map_atlas ||
                !shadow_map_atlas_subresource.IsValid() ||
                shadow_map_atlas_size.x != render_data.shadow_map_atlas_size.x ||
                shadow_map_atlas_size.y != render_data.shadow_map_atlas_size.y;

            if (regen_shadowmap_atlas)
            {
                RHITextureDesc shadow_map_atlas_desc = {};
                shadow_map_atlas_desc.width = render_data.shadow_map_atlas_size.x;
                shadow_map_atlas_desc.height = render_data.shadow_map_atlas_size.y;
                shadow_map_atlas_desc.depth = 1;
                shadow_map_atlas_desc.mip_levels = 1;
                shadow_map_atlas_desc.array_layers = 1;
                shadow_map_atlas_desc.sample_count = 1;
                shadow_map_atlas_desc.format = RHIFormat::D32Float;
                shadow_map_atlas_desc.usage = RHIResourceUsage::Default;
                shadow_map_atlas_desc.bind_flags = RHIBindFlags::DepthStencil;
                shadow_map_atlas = device->CreateTexture(shadow_map_atlas_desc);
                if (!shadow_map_atlas)
                {
                    backlog::Post("failed to create shadow map atlas", backlog::LogLevel::Error);
                    return;
                }

                shadow_map_atlas_subresource = {};
                RHISubresourceDesc shadow_map_atlas_subresource_desc = {};
                shadow_map_atlas_subresource_desc.type = RHISubresourceType::DepthStencil;
                shadow_map_atlas_subresource_desc.format = shadow_map_atlas_desc.format;
                if (!device->CreateSubresource(*shadow_map_atlas, shadow_map_atlas_subresource_desc, &shadow_map_atlas_subresource))
                {
                    backlog::Post("failed to create shadow map atlas subresource", backlog::LogLevel::Error);
                    shadow_map_atlas = nullptr;
                    return;
                }

                shadow_map_atlas_size = render_data.shadow_map_atlas_size;
            }
        }

        frame_context.command_list->TransitionResource(*back_buffer, RHIResourceState::RenderTarget);
        frame_context.command_list->TransitionResource(*depth_buffer, RHIResourceState::DepthWrite);
        frame_context.command_list->ClearRenderTarget(back_buffer_binding, { 0.f, 0.3f, 0.3f, 1.f });
        frame_context.command_list->ClearDepthStencil(depth_buffer_binding, 0.0f, 0u);

        RHIViewport viewport = {};
        viewport.x = static_cast<float>(view.viewport.x);
        viewport.y = static_cast<float>(view.viewport.y);
        viewport.width = static_cast<float>(view.viewport.width);
        viewport.height = static_cast<float>(view.viewport.height);
        viewport.min_depth = 0.0f;
        viewport.max_depth = 1.0f;
        frame_context.command_list->SetViewport(viewport);

        RHIRect scissor = {};
        scissor.x = view.scissor.x;
        scissor.y = view.scissor.y;
        scissor.width = view.scissor.width;
        scissor.height = view.scissor.height;
        frame_context.command_list->SetScissor(scissor);

        if (shadow_map_atlas && shadow_map_atlas_subresource.IsValid() && !render_data.shadow_light_entities.empty())
        {
            frame_context.command_list->BeginEvent("Fill Shadow Map Atlas");

            RHISubresourceBinding shadow_map_atlas_binding = {};
            shadow_map_atlas_binding.resource = shadow_map_atlas.get();
            shadow_map_atlas_binding.subresource = shadow_map_atlas_subresource;

            frame_context.command_list->TransitionResource(*shadow_map_atlas, RHIResourceState::DepthWrite);
            frame_context.command_list->ClearDepthStencil(shadow_map_atlas_binding, 0.0f, 0u);
            frame_context.command_list->SetRenderTargets({}, &shadow_map_atlas_binding);

            // TODO: unify with ShaderLight
            for (const Entity shadow_light_entity : render_data.shadow_light_entities)
            {
                const LightComponent* shadow_light = view.scene->GetComponent<LightComponent>(shadow_light_entity);
                if (!shadow_light || !shadow_light->HasShadowMapAtlasRect())
                {
                    continue;
                }

                ShaderCamera shadow_camera = {};
                shadow_camera.Init();

                const XMVECTOR light_position = XMLoadFloat3(&shadow_light->position);
                XMVECTOR light_direction = XMLoadFloat3(&shadow_light->direction);
                if (XMVectorGetX(XMVector3LengthSq(light_direction)) <= FLT_EPSILON)
                {
                    light_direction = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                }
                light_direction = XMVector3Normalize(light_direction);

                XMVECTOR light_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                if (std::abs(XMVectorGetX(XMVector3Dot(light_up, light_direction))) > 0.99f)
                {
                    light_up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                }

                XMMATRIX shadow_view = {};
                XMMATRIX shadow_projection = {};

                if (shadow_light->type == LightComponent::Directional)
                {
                    const XMVECTOR shadow_eye = light_position - light_direction * 100.0f;
                    shadow_view = XMMatrixLookToLH(shadow_eye, light_direction, light_up);
                    // TODO: current projection size is hard coded.
                    shadow_projection = XMMatrixOrthographicLH(20.0f, 20.0f, 1000.0f, 0.1f);
                    XMStoreFloat3(&shadow_camera.position, shadow_eye);
                    shadow_camera.z_near = 0.1f;
                    shadow_camera.z_far = 1000.0f;
                }
                else if (shadow_light->type == LightComponent::Spot)
                {
                    shadow_view = XMMatrixLookToLH(light_position, light_direction, light_up);
                    shadow_projection = XMMatrixPerspectiveFovLH((std::max)(0.1f, shadow_light->outer_cone_angle * 2.0f), 1.0f, shadow_light->range, 0.1f);
                    XMStoreFloat3(&shadow_camera.position, light_position);
                    shadow_camera.z_near = 0.1f;
                    shadow_camera.z_far = shadow_light->range;
                }
                else if (shadow_light->type == LightComponent::Point)
                {
                    shadow_view = XMMatrixLookToLH(light_position, light_direction, light_up);
                    shadow_projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, shadow_light->range, 0.1f);
                    XMStoreFloat3(&shadow_camera.position, light_position);
                    shadow_camera.z_near = 0.1f;
                    shadow_camera.z_far = shadow_light->range;
                }
                else
                {
                    continue;
                }

                XMStoreFloat3(&shadow_camera.forward, light_direction);
                XMStoreFloat3(&shadow_camera.up, XMVector3Normalize(light_up));
                XMStoreFloat4x4(&shadow_camera.view, shadow_view);
                XMStoreFloat4x4(&shadow_camera.projection, shadow_projection);
                XMStoreFloat4x4(&shadow_camera.view_projection, XMMatrixMultiply(shadow_view, shadow_projection));

                if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shadow_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer))
                {
                    return;
                }

                RHIViewport shadow_viewport = {};
                shadow_viewport.x = static_cast<float>(shadow_light->shadow_map_atlas_rect.x);
                shadow_viewport.y = static_cast<float>(shadow_light->shadow_map_atlas_rect.y);
                shadow_viewport.width = static_cast<float>(shadow_light->shadow_map_atlas_rect.z);
                shadow_viewport.height = static_cast<float>(shadow_light->shadow_map_atlas_rect.w);
                shadow_viewport.min_depth = 0.0f;
                shadow_viewport.max_depth = 1.0f;
                frame_context.command_list->SetViewport(shadow_viewport);

                RHIRect shadow_scissor = {};
                shadow_scissor.x = shadow_light->shadow_map_atlas_rect.x;
                shadow_scissor.y = shadow_light->shadow_map_atlas_rect.y;
                shadow_scissor.width = shadow_light->shadow_map_atlas_rect.z;
                shadow_scissor.height = shadow_light->shadow_map_atlas_rect.w;
                frame_context.command_list->SetScissor(shadow_scissor);

                DrawScene(view, frame_context, RenderPassType::ShadowPass, DrawScene_Opaque | DrawScene_ShadowCaster);
            }
            frame_context.command_list->EndEvent();

            frame_context.command_list->BeginEvent("Restore Render State");
            ShaderCamera shader_camera{};
            shader_camera.Init();
            shader_camera.view = IDENTITY_MATRIX;
            shader_camera.projection = IDENTITY_MATRIX;
            shader_camera.view_projection = IDENTITY_MATRIX;
            if (view.camera_entity != ecs::INVALID_ENTITY)
            {
                const ecs::CameraComponent* camera_component = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
                if (camera_component)
                {
                    shader_camera.position = camera_component->eye;
                    shader_camera.forward = camera_component->forward;
                    shader_camera.up = camera_component->up;
                    shader_camera.z_near = camera_component->near;
                    shader_camera.z_far = camera_component->far;
                    shader_camera.view = camera_component->view;
                    shader_camera.projection = camera_component->projection;
                    shader_camera.view_projection = camera_component->view_projection;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shader_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer))
            {
                return;
            }

            frame_context.command_list->SetViewport(viewport);
            frame_context.command_list->SetScissor(scissor);

            frame_context.command_list->EndEvent();
        }

        // prepass
        {
            frame_context.command_list->BeginEvent("Prepass");
            frame_context.command_list->SetRenderTargets({}, & depth_buffer_binding);
            DrawScene(view, frame_context, RenderPassType::DepthPrepass, DrawScene_Opaque);
            frame_context.command_list->EndEvent();
        }
        
        // main pass
        {
            frame_context.command_list->BeginEvent("Restore Camera State");

            frame_context.command_list->SetRenderTargets(color_targets, &depth_buffer_binding);
            DrawScene(view, frame_context, RenderPassType::MainPass, DrawScene_Opaque | DrawScene_Transparent);
            frame_context.command_list->EndEvent();
        }
    }

    void ForwardRenderer::EndFrame()
    {
        FrameContext& frame_context = GetFrameContext();

        std::shared_ptr<RHISwapchain> swapchain = current_window->GetRHISwapchain();
        if (!swapchain)
        {
            return;
        }

        std::shared_ptr<RHIResource> back_buffer = swapchain->GetCurrentBackBuffer();

        if (!back_buffer)
        {
            return;
        }

        frame_context.command_list->TransitionResource(*back_buffer, RHIResourceState::Present);

        const std::shared_ptr<RHIContext> graphics_context = device->GetContext(RHIQueueType::Graphics);
        frame_context.command_list->End();
        frame_context.fence_value = graphics_context->Submit(*frame_context.command_list, frame_context.fence.get());

        if (!swapchain->Present())
        {
            backlog::Post("failed to present swapchain", backlog::LogLevel::Error);
            return;
        }

        ++frame_count;
        current_frame_slot = (current_frame_slot + 1) % static_cast<uint32>(frame_contexts.size());

    }

    void ForwardRenderer::WaitIdle()
    {
        for (Size i = 0; i < static_cast<Size>(RHIQueueType::Count); ++i)
        {
            auto context = device->GetContext(static_cast<RHIQueueType>(i));
            context->WaitIdle();
        }
    }

    void ForwardRenderer::Shutdown()
    {
        WaitIdle();

        current_window = nullptr;
        frame_contexts = {};
        current_frame_slot = 0;
        shader_instance_default_buffer_subresource = {};
        shader_instance_default_buffer = nullptr;
        shader_geometry_default_buffer_subresource = {};
        shader_geometry_default_buffer = nullptr;
        shader_material_default_buffer_subresource = {};
        shader_material_default_buffer = nullptr;
        shader_light_default_buffer_subresource = {};
        shader_light_default_buffer = nullptr;
        shader_frame_buffer_subresource = {};
        shader_frame_buffer = nullptr;
        shader_camera_buffer_subresource = {};
        shader_camera_buffer = nullptr;
        shadow_map_atlas_subresource = {};
        shadow_map_atlas = nullptr;
        shadow_map_atlas_size = { 0, 0 };
        back_buffer_subresources = {};
        depth_buffer_subresource = {};
        depth_buffer = nullptr;
        device.reset();
    }
}
