#include "ForwardRenderer.h"

#include "Backlog.h"
#include "Scene.h"

#include "Window.h"
#include "ShaderLibrary.h"
#include "Entity.h"

#include <cstring>

namespace won::rendering
{
    static won::resource::ShaderLibrary shader_library;

    namespace
    {
        float4x4 CreateIdentityFloat4x4()
        {
            float4x4 result = {};
            result._11 = 1.0f;
            result._22 = 1.0f;
            result._33 = 1.0f;
            result._44 = 1.0f;
            return result;
        }
    }

    bool ForwardRenderer::BuildFrameContext(const View& view, FrameContext& frame_context)
    {
        const ecs::Scene::RenderData& render_data = view.scene->GetRenderData();
        const Vector<ShaderInstance>& shader_instance = render_data.shader_instance;
        const Vector<ShaderGeometry>& shader_geometry = render_data.shader_geometry;
        const Vector<ShaderMaterial>& shader_material = render_data.shader_material;

        const Size required_instance_buffer_size = shader_instance.size() * sizeof(ShaderInstance);
        const Size required_geometry_buffer_size = shader_geometry.size() * sizeof(ShaderGeometry);
        const Size required_material_buffer_size = shader_material.size() * sizeof(ShaderMaterial);

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
                shader_instance_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::CopyDest;
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
                shader_instance_upload_buffer_desc.bind_flags = RHIBindFlags::CopySource;
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
                shader_geometry_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::CopyDest;
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
                shader_geometry_upload_buffer_desc.bind_flags = RHIBindFlags::CopySource;
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
                shader_material_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::CopyDest;
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
                shader_material_upload_buffer_desc.bind_flags = RHIBindFlags::CopySource;
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

        return true;
    }

    void ForwardRenderer::Initialize(const RendererDesc& desc)
    {
        device = desc.device;

        if (!shader_library.LoadAllShaders())
        {
            backlog::Post("ForwardRenderer failed to load test shaders", backlog::LogLevel::Error);
            return;
        }

        const std::shared_ptr<RHIShader> vertex_shader = shader_library.GetShader(resource::ShaderId::TestTriangleVS);
        const std::shared_ptr<RHIShader> pixel_shader = shader_library.GetShader(resource::ShaderId::TestRedPS);

        RHIGraphicsPipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = vertex_shader.get();
        pipeline_desc.pixel_shader = pixel_shader.get();
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;

        test_pipeline = device->CreateGraphicsPipeline(pipeline_desc);
        if (!test_pipeline)
        {
            backlog::Post("failed to create test graphics pipeline", backlog::LogLevel::Error);
            return;
        }

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
        shader_frame_buffer_desc.usage = RHIResourceUsage::Upload;
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
        shader_camera_buffer_desc.usage = RHIResourceUsage::Upload;
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

        ++frame_count;

        FrameContext& frame_context = frame_contexts[current_frame_slot];
        if (frame_context.fence_value > 0)
        {
            frame_context.fence->Wait(frame_context.fence_value);
            frame_context.fence_value = 0;
        }
        frame_context.command_allocator->Reset();
        frame_context.command_list->Begin(*frame_context.command_allocator);

        if (!BuildFrameContext(view, frame_context))
        {
            return;
        }

        if (!shader_frame_buffer || !shader_camera_buffer)
        {
            backlog::Post("failed to bind frame/camera constants because buffers are missing", backlog::LogLevel::Error);
            return;
        }

        ShaderFrame shader_frame = {};
        shader_frame.scene.instancebuffer = shader_instance_default_buffer_subresource.descriptor_index;
        shader_frame.scene.geometrybuffer = shader_geometry_default_buffer_subresource.descriptor_index;
        shader_frame.scene.materialbuffer = shader_material_default_buffer_subresource.descriptor_index;

        ShaderCamera shader_camera = {};
        shader_camera.view = CreateIdentityFloat4x4();
        shader_camera.projection = CreateIdentityFloat4x4();
        shader_camera.view_projection = CreateIdentityFloat4x4();

        void* shader_frame_mapped_data = shader_frame_buffer->GetMappedData();
        void* shader_camera_mapped_data = shader_camera_buffer->GetMappedData();
        if (!shader_frame_mapped_data || !shader_camera_mapped_data)
        {
            backlog::Post("failed to access frame/camera constant buffer mapped data", backlog::LogLevel::Error);
            return;
        }

        std::memcpy(shader_frame_mapped_data, &shader_frame, sizeof(ShaderFrame));
        std::memcpy(shader_camera_mapped_data, &shader_camera, sizeof(ShaderCamera));

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
        Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };
        frame_context.command_list->TransitionResource(*back_buffer, RHIResourceState::RenderTarget);
        frame_context.command_list->SetRenderTargets(color_targets, nullptr);
        frame_context.command_list->ClearRenderTarget(back_buffer_binding, { 0.f, 0.3f, 0.3f, 1.f });
        // test 
        if (test_pipeline)
        {
            const ecs::Scene::RenderData& render_data = view.scene->GetRenderData();
            frame_context.command_list->SetGraphicsPipeline(*test_pipeline);
            RHISubresourceBinding shader_frame_binding = {};
            shader_frame_binding.resource = shader_frame_buffer.get();
            shader_frame_binding.subresource = shader_frame_buffer_subresource;
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_frame_binding);

            RHISubresourceBinding shader_camera_binding = {};
            shader_camera_binding.resource = shader_camera_buffer.get();
            shader_camera_binding.subresource = shader_camera_buffer_subresource;
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 2, shader_camera_binding);
            frame_context.command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            for (const auto& renderable : render_data.renderables)
            {
                if (renderable.push_constants.geometry_index >= render_data.shader_geometry.size())
                {
                    continue;
                }

                if (!renderable.index_buffer || renderable.index_count == 0 || renderable.index_buffer_size == 0)
                {
                    continue;
                }

                frame_context.command_list->SetIndexBuffer(*renderable.index_buffer, sizeof(uint32), renderable.index_buffer_offset, renderable.index_buffer_size);
                frame_context.command_list->PushConstants(RHIShaderStage::Vertex, &renderable.push_constants, sizeof(ObjectPushConstants), 0);
                frame_context.command_list->DrawIndexed(renderable.index_count, 1, 0, 0, 0);
            }
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

        current_frame_slot = (current_frame_slot + 1) % static_cast<uint32>(frame_contexts.size());

        // TODO: build render snapshot and submit passes.
    }

    void ForwardRenderer::EndFrame()
    {
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
        shader_frame_buffer_subresource = {};
        shader_frame_buffer = nullptr;
        shader_camera_buffer_subresource = {};
        shader_camera_buffer = nullptr;
        test_pipeline = nullptr;
        device.reset();
    }
}
