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
        Size buffer_size = 0;
        if (frame_context.frame_upload_buffer)
        {
            buffer_size = frame_context.frame_upload_buffer.get()->GetDesc().buffer_desc.size;
        }
        
        Size aligned_offset = align(frame_context.frame_upload_offset, alignment);
        Size required_size = aligned_offset + size;

        if (buffer_size < required_size)
        {
            RHIBufferDesc frame_upload_buffer_desc = {};
            frame_upload_buffer_desc.size = align(std::max((buffer_size + size) * 2, (Size)1024 * 20), alignment); // initial_size
            frame_upload_buffer_desc.usage = RHIResourceUsage::Upload;
            frame_upload_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::VertexBuffer | RHIBindFlags::IndexBuffer;
            frame_context.frame_upload_buffer = device->CreateBuffer(frame_upload_buffer_desc);

            frame_context.frame_upload_offset = 0;
        }

        void* mapped_data = frame_context.frame_upload_buffer->GetMappedData();

        out_allocation.mapped_data = static_cast<uint8*>(mapped_data) + aligned_offset;
        out_allocation.buffer_offset = aligned_offset;
        frame_context.frame_upload_offset = aligned_offset + size;

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
        current_window = &window;
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

        // TODO : seperate resize logic
        const RHIResourceDesc& back_buffer_desc = back_buffer->GetDesc();
        const uint32 target_width = back_buffer_desc.texture_desc.width;
        const uint32 target_height = back_buffer_desc.texture_desc.height;
        const uint32 target_sample_count = back_buffer_desc.texture_desc.sample_count > 0 ? back_buffer_desc.texture_desc.sample_count : 1;
        if (!depth_buffer || !depth_buffer_subresource.IsValid() || depth_buffer_width != target_width || depth_buffer_height != target_height || depth_buffer_sample_count != target_sample_count)
        {
            RHITextureDesc depth_desc = {};
            depth_desc.width = target_width;
            depth_desc.height = target_height;
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

            depth_buffer_width = target_width;
            depth_buffer_height = target_height;
            depth_buffer_sample_count = target_sample_count;
        }

        FrameContext& frame_context = GetFrameContext();
        if (frame_context.fence_value > 0)
        {
            frame_context.fence->Wait(frame_context.fence_value);
            frame_context.fence_value = 0;
        }
        frame_context.command_allocator->Reset();
        frame_context.command_list->Begin(*frame_context.command_allocator);
        frame_context.frame_upload_offset = 0;

        if (!BuildFrameContext(view, frame_context))
        {
            return;
        }

        RHISubresourceDesc back_buffer_rtv_desc = {};
        back_buffer_rtv_desc.type = RHISubresourceType::RenderTarget;
        RHISubresourceHandle back_buffer_rtv = {};
        if (!device->CreateSubresource(*back_buffer, back_buffer_rtv_desc, &back_buffer_rtv))
        {
            backlog::Post("failed to create back buffer RTV", backlog::LogLevel::Error);
            return;
        }

        RHISubresourceBinding back_buffer_binding = {};
        back_buffer_binding.resource = back_buffer.get();
        back_buffer_binding.subresource = back_buffer_rtv;
        RHISubresourceBinding depth_buffer_binding = {};
        depth_buffer_binding.resource = depth_buffer.get();
        depth_buffer_binding.subresource = depth_buffer_subresource;
        Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };
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

        // prepass
        {
            frame_context.command_list->SetRenderTargets({}, & depth_buffer_binding);
            DrawScene(view, frame_context, RenderPassType::DepthPrepass, DrawScene_Opaque);
        }
        
        // main pass
        {
            frame_context.command_list->SetRenderTargets(color_targets, &depth_buffer_binding);
            DrawScene(view, frame_context, RenderPassType::MainPass, DrawScene_Opaque | DrawScene_Transparent);
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

    void ForwardRenderer::Shutdown()
    {
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
        depth_buffer_subresource = {};
        depth_buffer = nullptr;
        depth_buffer_width = 0;
        depth_buffer_height = 0;
        depth_buffer_sample_count = 1;
        device.reset();
    }
}
