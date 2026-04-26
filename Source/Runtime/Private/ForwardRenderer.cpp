#include "ForwardRenderer.h"

#include "Backlog.h"
#include "Profiler.h"
#include "Scene.h"
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
        void RemoveResourceDeferred(Renderer::FrameContext& frame_context, std::shared_ptr<RHIResource>& resource)
        {
            frame_context.deferred_res_removal.push_back(resource);
            resource = nullptr;
        }
    }

    RendererDebugState ForwardRenderer::GetDebugState() const
    {
        return debug_state;
    }

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
            if (frame_context.frame_upload_buffer)
            {
                frame_context.frame_upload_buffer->SetName("Frame Upload Buffer");
            }
            frame_context.frame_upload_offset = 0;
        }

        Size buffer_size = frame_context.frame_upload_buffer->GetDesc().buffer_desc.size;
        aligned_offset = align(frame_context.frame_upload_offset, alignment);
        Size required_size = aligned_offset + size;
        
        if (buffer_size < required_size)
        {
            RHIBufferDesc new_desc = frame_context.frame_upload_buffer->GetDesc().buffer_desc;
            new_desc.size = align(required_size * 2, alignment);
            RemoveResourceDeferred(frame_context, frame_context.frame_upload_buffer);
            frame_context.frame_upload_buffer = device->CreateBuffer(new_desc);
            if (frame_context.frame_upload_buffer)
            {
                frame_context.frame_upload_buffer->SetName("Frame Upload Buffer");
            }
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
        const Vector<ShaderInstance>& shader_instances = render_data.shader_instances;
        const Vector<ShaderGeometry>& shader_geometries = render_data.shader_geometries;
        const Vector<ShaderMaterial>& shader_materials = render_data.shader_materials;
        const Vector<ShaderLight>& shader_lights = render_data.shader_lights;
        const Vector<ShaderShadowCascade>& shader_shadow_cascades = render_data.shader_shadow_cascades;
        const Vector<ShaderBVHNode>& shader_bvh_nodes = render_data.shader_bvh_nodes;
        const Vector<ShaderBVHPrimitive>& shader_bvh_primitives = render_data.shader_bvh_primitives;

        const Size required_instance_buffer_size = shader_instances.size() * sizeof(ShaderInstance);
        const Size required_geometry_buffer_size = shader_geometries.size() * sizeof(ShaderGeometry);
        const Size required_material_buffer_size = shader_materials.size() * sizeof(ShaderMaterial);
        const Size required_light_buffer_size = shader_lights.size() * sizeof(ShaderLight);
        const Size required_shadow_cascade_buffer_size = shader_shadow_cascades.size() * sizeof(ShaderShadowCascade);
        const Size required_bvh_node_buffer_size = shader_bvh_nodes.size() * sizeof(ShaderBVHNode);
        const Size required_bvh_primitive_buffer_size = shader_bvh_primitives.size() * sizeof(ShaderBVHPrimitive);

        if (required_instance_buffer_size == 0)
        {
            frame_context.shader_instance_upload_buffer = nullptr;
            RemoveResourceDeferred(frame_context, shader_instance_default_buffer);
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
                RemoveResourceDeferred(frame_context, shader_instance_default_buffer);
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

            void* mapped_data = frame_context.shader_instance_upload_buffer->GetMappedData();
            if (!mapped_data)
            {
                backlog::Post("failed to access mapped instance upload buffer", backlog::LogLevel::Error);
                return false;
            }
            std::memcpy(mapped_data, shader_instances.data(), required_instance_buffer_size);

            frame_context.command_list->TransitionResource(*shader_instance_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_instance_default_buffer, *frame_context.shader_instance_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_instance_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_geometry_buffer_size == 0)
        {
            frame_context.shader_geometry_upload_buffer = nullptr;
            RemoveResourceDeferred(frame_context, shader_geometry_default_buffer);
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
                RemoveResourceDeferred(frame_context, shader_geometry_default_buffer);
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

            frame_context.command_list->TransitionResource(*shader_geometry_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_geometry_default_buffer, *frame_context.shader_geometry_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_geometry_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_material_buffer_size == 0)
        {
            frame_context.shader_material_upload_buffer = nullptr;
            RemoveResourceDeferred(frame_context, shader_material_default_buffer);
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
                RemoveResourceDeferred(frame_context, shader_material_default_buffer);
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

            frame_context.command_list->TransitionResource(*shader_material_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_material_default_buffer, *frame_context.shader_material_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_material_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_light_buffer_size == 0)
        {
            frame_context.shader_light_upload_buffer = nullptr;
            RemoveResourceDeferred(frame_context, shader_light_default_buffer);
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
                RemoveResourceDeferred(frame_context, shader_light_default_buffer);
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

            frame_context.command_list->TransitionResource(*shader_light_default_buffer, RHIResourceState::CopyDest);
            frame_context.command_list->CopyResource(*shader_light_default_buffer, *frame_context.shader_light_upload_buffer);
            frame_context.command_list->TransitionResource(*shader_light_default_buffer, RHIResourceState::ShaderRead);
        }

        if (required_shadow_cascade_buffer_size == 0)
        {
            RemoveResourceDeferred(frame_context, shader_shadow_cascade_default_buffer);
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
                RemoveResourceDeferred(frame_context, shader_shadow_cascade_default_buffer);
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

            if (!UpdateDefaultBuffer(frame_context, *shader_shadow_cascade_default_buffer, shader_shadow_cascades.data(), required_shadow_cascade_buffer_size, RHIResourceState::ShaderRead))
            {
                return false;
            }
        }

        if (required_bvh_node_buffer_size == 0)
        {
            RemoveResourceDeferred(frame_context, shader_bvh_node_default_buffer);
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
                RemoveResourceDeferred(frame_context, shader_bvh_node_default_buffer);
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

            if (!UpdateDefaultBuffer(frame_context, *shader_bvh_node_default_buffer, shader_bvh_nodes.data(), required_bvh_node_buffer_size, RHIResourceState::ShaderRead))
            {
                return false;
            }
        }

        if (required_bvh_primitive_buffer_size == 0)
        {
            RemoveResourceDeferred(frame_context, shader_bvh_primitive_default_buffer);
            shader_bvh_primitive_default_buffer_srv = {};
        }
        else
        {
            Size current_default_buffer_size = 0;
            if (shader_bvh_primitive_default_buffer)
            {
                current_default_buffer_size = shader_bvh_primitive_default_buffer->GetDesc().buffer_desc.size;
            }

            if (!shader_bvh_primitive_default_buffer || current_default_buffer_size < required_bvh_primitive_buffer_size)
            {
                RemoveResourceDeferred(frame_context, shader_bvh_primitive_default_buffer);
                RHIBufferDesc shader_bvh_primitive_default_buffer_desc = {};
                shader_bvh_primitive_default_buffer_desc.size = required_bvh_primitive_buffer_size;
                shader_bvh_primitive_default_buffer_desc.usage = RHIResourceUsage::Default;
                shader_bvh_primitive_default_buffer_desc.bind_flags = RHIBindFlags::ShaderResource;
                shader_bvh_primitive_default_buffer = device->CreateBuffer(shader_bvh_primitive_default_buffer_desc);
                if (!shader_bvh_primitive_default_buffer)
                {
                    backlog::Post("failed to create shader bvh primitive default buffer", backlog::LogLevel::Error);
                    return false;
                }
                shader_bvh_primitive_default_buffer->SetName("Shader BVH Primitive Default Buffer");

                shader_bvh_primitive_default_buffer_srv = {};
                RHISubresourceDesc shader_bvh_primitive_default_subresource_desc = {};
                shader_bvh_primitive_default_subresource_desc.type = RHISubresourceType::ShaderResource;
                shader_bvh_primitive_default_subresource_desc.buffer_offset = 0;
                shader_bvh_primitive_default_subresource_desc.buffer_size = shader_bvh_primitive_default_buffer->GetDesc().buffer_desc.size;
                shader_bvh_primitive_default_subresource_desc.buffer_stride = sizeof(ShaderBVHPrimitive);
                if (!device->CreateSubresource(*shader_bvh_primitive_default_buffer, shader_bvh_primitive_default_subresource_desc, &shader_bvh_primitive_default_buffer_srv))
                {
                    backlog::Post("failed to create shader bvh primitive default subresource", backlog::LogLevel::Error);
                    shader_bvh_primitive_default_buffer = nullptr;
                    return false;
                }
            }

            if (!UpdateDefaultBuffer(frame_context, *shader_bvh_primitive_default_buffer, shader_bvh_primitives.data(), required_bvh_primitive_buffer_size, RHIResourceState::ShaderRead))
            {
                return false;
            }
        }

        if ((render_data.shader_ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) == 0)
        {
            RemoveResourceDeferred(frame_context, ddgi_irradiance_texture);
            RemoveResourceDeferred(frame_context, ddgi_visibility_texture);
            ddgi_irradiance_texture_srv = {};
            ddgi_irradiance_texture_uav = {};
            ddgi_visibility_texture_srv = {};
            ddgi_visibility_texture_uav = {};
            ddgi_probe_counts = { 0, 0, 0 };
        }
        else
        {
            const bool recreate_ddgi_texture =
                !ddgi_irradiance_texture ||
                !ddgi_irradiance_texture_srv.IsValid() ||
                !ddgi_irradiance_texture_uav.IsValid() ||
                !ddgi_visibility_texture ||
                !ddgi_visibility_texture_srv.IsValid() ||
                !ddgi_visibility_texture_uav.IsValid() ||
                ddgi_probe_counts.x != render_data.shader_ddgi_volume.probe_counts.x ||
                ddgi_probe_counts.y != render_data.shader_ddgi_volume.probe_counts.y ||
                ddgi_probe_counts.z != render_data.shader_ddgi_volume.probe_counts.z;

            if (recreate_ddgi_texture)
            {
                RemoveResourceDeferred(frame_context, ddgi_irradiance_texture);
                RemoveResourceDeferred(frame_context, ddgi_visibility_texture);
                ddgi_irradiance_texture_srv = {};
                ddgi_irradiance_texture_uav = {};
                ddgi_visibility_texture_srv = {};
                ddgi_visibility_texture_uav = {};
                RHITextureDesc ddgi_irradiance_texture_desc = {};
                ddgi_irradiance_texture_desc.width = (std::max)(render_data.shader_ddgi_volume.probe_counts.x, 1u) * DDGI_IRRADIANCE_RESOLUTION;
                ddgi_irradiance_texture_desc.height = (std::max)(render_data.shader_ddgi_volume.probe_counts.y, 1u) * (std::max)(render_data.shader_ddgi_volume.probe_counts.z, 1u) * DDGI_IRRADIANCE_RESOLUTION;
                ddgi_irradiance_texture_desc.depth = 1;
                ddgi_irradiance_texture_desc.mip_levels = 1;
                ddgi_irradiance_texture_desc.array_layers = 1;
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

                ddgi_irradiance_texture_srv = {};
                RHISubresourceDesc ddgi_irradiance_srv_desc = {};
                ddgi_irradiance_srv_desc.type = RHISubresourceType::ShaderResource;
                ddgi_irradiance_srv_desc.format = ddgi_irradiance_texture_desc.format;
                ddgi_irradiance_srv_desc.first_mip = 0;
                ddgi_irradiance_srv_desc.mip_count = 1;
                if (!device->CreateSubresource(*ddgi_irradiance_texture, ddgi_irradiance_srv_desc, &ddgi_irradiance_texture_srv))
                {
                    backlog::Post("failed to create ddgi irradiance srv", backlog::LogLevel::Error);
                    ddgi_irradiance_texture = nullptr;
                    return false;
                }

                ddgi_irradiance_texture_uav = {};
                RHISubresourceDesc ddgi_irradiance_uav_desc = {};
                ddgi_irradiance_uav_desc.type = RHISubresourceType::UnorderedAccess;
                ddgi_irradiance_uav_desc.format = ddgi_irradiance_texture_desc.format;
                ddgi_irradiance_uav_desc.first_mip = 0;
                ddgi_irradiance_uav_desc.mip_count = 1;
                ddgi_irradiance_uav_desc.first_slice = 0;
                ddgi_irradiance_uav_desc.slice_count = 1;
                if (!device->CreateSubresource(*ddgi_irradiance_texture, ddgi_irradiance_uav_desc, &ddgi_irradiance_texture_uav))
                {
                    backlog::Post("failed to create ddgi irradiance uav", backlog::LogLevel::Error);
                    ddgi_irradiance_texture = nullptr;
                    return false;
                }

                ddgi_probe_counts = render_data.shader_ddgi_volume.probe_counts;

                RHITextureDesc ddgi_visibility_texture_desc = {};
                ddgi_visibility_texture_desc.width = (std::max)(render_data.shader_ddgi_volume.probe_counts.x, 1u) * DDGI_VISIBILITY_RESOLUTION;
                ddgi_visibility_texture_desc.height = (std::max)(render_data.shader_ddgi_volume.probe_counts.y, 1u) * (std::max)(render_data.shader_ddgi_volume.probe_counts.z, 1u) * DDGI_VISIBILITY_RESOLUTION;
                ddgi_visibility_texture_desc.depth = 1;
                ddgi_visibility_texture_desc.mip_levels = 1;
                ddgi_visibility_texture_desc.array_layers = 1;
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

                ddgi_visibility_texture_srv = {};
                RHISubresourceDesc ddgi_visibility_srv_desc = {};
                ddgi_visibility_srv_desc.type = RHISubresourceType::ShaderResource;
                ddgi_visibility_srv_desc.format = ddgi_visibility_texture_desc.format;
                ddgi_visibility_srv_desc.first_mip = 0;
                ddgi_visibility_srv_desc.mip_count = 1;
                if (!device->CreateSubresource(*ddgi_visibility_texture, ddgi_visibility_srv_desc, &ddgi_visibility_texture_srv))
                {
                    backlog::Post("failed to create ddgi visibility srv", backlog::LogLevel::Error);
                    ddgi_visibility_texture = nullptr;
                    return false;
                }

                ddgi_visibility_texture_uav = {};
                RHISubresourceDesc ddgi_visibility_uav_desc = {};
                ddgi_visibility_uav_desc.type = RHISubresourceType::UnorderedAccess;
                ddgi_visibility_uav_desc.format = ddgi_visibility_texture_desc.format;
                ddgi_visibility_uav_desc.first_mip = 0;
                ddgi_visibility_uav_desc.mip_count = 1;
                ddgi_visibility_uav_desc.first_slice = 0;
                ddgi_visibility_uav_desc.slice_count = 1;
                if (!device->CreateSubresource(*ddgi_visibility_texture, ddgi_visibility_uav_desc, &ddgi_visibility_texture_uav))
                {
                    backlog::Post("failed to create ddgi visibility uav", backlog::LogLevel::Error);
                    ddgi_visibility_texture = nullptr;
                    return false;
                }
            }
        }

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
        shader_frame.scene.bvh_primitive_buffer = shader_bvh_primitive_default_buffer_srv.descriptor_index;
        shader_frame.scene.bvh_node_count = static_cast<uint32>(shader_bvh_nodes.size());
        shader_frame.scene.bvh_primitive_count = static_cast<uint32>(shader_bvh_primitives.size());
        shader_frame.sky = render_data.shader_sky;
        shader_frame.environment_lighting = render_data.shader_environment_lighting;
        shader_frame.ddgi_volume = render_data.shader_ddgi_volume;
        shader_frame.ddgi_volume.irradiance_texture = ddgi_irradiance_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.irradiance_texture_uav = ddgi_irradiance_texture_uav.descriptor_index;
        shader_frame.ddgi_volume.visibility_texture = ddgi_visibility_texture_srv.descriptor_index;
        shader_frame.ddgi_volume.visibility_texture_uav = ddgi_visibility_texture_uav.descriptor_index;

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

    bool ForwardRenderer::BuildShadowCascades(const View& view)
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

    bool ForwardRenderer::DrawScene(const View& view, const FrameContext& frame_context, RenderPassType pass, uint32 flags)
    {
        const Scene::RenderData& render_data = view.scene->GetRenderData();

        frame_context.command_list->SetGraphicsPipeline(*shader_library->GetPipeline(pass).get());

        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_cbv;
        frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);

        RHISubresourceBinding shader_camera_binding = {};
        shader_camera_binding.resource = shader_camera_buffer.get();
        shader_camera_binding.subresource = shader_camera_buffer_cbv;
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

    void ForwardRenderer::BeginFrame(platform::Window& window)
    {
        if (current_window != &window)
        {
            WaitIdle();
            back_buffers_rtv = {};
            depth_buffer_dsv = {};
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

        back_buffers_rtv = {};
        depth_buffer_dsv = {};
        depth_buffer = nullptr;

        if (!swapchain->Resize(width, height))
        {
            backlog::Post("failed to resize swapchain", backlog::LogLevel::Error);
        }
    }

    void ForwardRenderer::Render(const View& view)
    {
        if (!view.scene || view.camera_entity == ecs::INVALID_ENTITY || !current_window)
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
                return;
            }

            RHISubresourceDesc back_buffer_subresource_desc = {};
            back_buffer_subresource_desc.type = RHISubresourceType::RenderTarget;
            if (!device->CreateSubresource(*swapchain_back_buffer, back_buffer_subresource_desc, &back_buffers_rtv[i]))
            {
                backlog::Post("failed to create back buffer RTV", backlog::LogLevel::Error);
                return;
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

        FrameContext& frame_context = GetFrameContext();
        if (frame_context.fence_value > 0)
        {
            frame_context.fence->Wait(frame_context.fence_value);
            frame_context.fence_value = 0;
        }

        frame_context.deferred_res_removal.clear();

        if (recreate_depth_buffer)
        {
            RemoveResourceDeferred(frame_context, depth_buffer);
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
            depth_buffer->SetName("Scene Depth Buffer");

            depth_buffer_dsv = {};
            RHISubresourceDesc depth_subresource_desc = {};
            depth_subresource_desc.type = RHISubresourceType::DepthStencil;
            depth_subresource_desc.format = depth_desc.format;
            if (!device->CreateSubresource(*depth_buffer, depth_subresource_desc, &depth_buffer_dsv))
            {
                backlog::Post("failed to create depth buffer subresource", backlog::LogLevel::Error);
                depth_buffer = nullptr;
                return;
            }
        }

        if (!BuildShadowCascades(view))
        {
            return;
        }

        const Scene::RenderData& render_data = view.scene->GetRenderData();
        debug_state.ddgi = {};
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
            debug_state.ddgi.total_probe_count = probe_counts.x * probe_counts.y * probe_counts.z;
        }
        debug_state.ddgi.irradiance_texture_allocated = ddgi_irradiance_texture != nullptr;
        debug_state.ddgi.irradiance_srv_valid = ddgi_irradiance_texture_srv.IsValid();
        debug_state.ddgi.irradiance_uav_valid = ddgi_irradiance_texture_uav.IsValid();
        debug_state.ddgi.visibility_texture_allocated = ddgi_visibility_texture != nullptr;
        debug_state.ddgi.visibility_srv_valid = ddgi_visibility_texture_srv.IsValid();
        debug_state.ddgi.visibility_uav_valid = ddgi_visibility_texture_uav.IsValid();
        debug_state.ddgi.irradiance_texture_srv = ddgi_irradiance_texture_srv.descriptor_index;
        debug_state.ddgi.irradiance_texture_uav = ddgi_irradiance_texture_uav.descriptor_index;
        debug_state.ddgi.visibility_texture_srv = ddgi_visibility_texture_srv.descriptor_index;
        debug_state.ddgi.visibility_texture_uav = ddgi_visibility_texture_uav.descriptor_index;
        if (render_data.shadow_map_atlas_size.x == 0 || render_data.shadow_map_atlas_size.y == 0)
        {
            RemoveResourceDeferred(frame_context, shadow_map_atlas);
            shadow_map_atlas_dsv = {};
            shadow_map_atlas_srv = {};
            shadow_map_atlas_size = { 0, 0 };
        }
        else
        {
            const bool recreate_shadowmap_atlas =
                !shadow_map_atlas ||
                !shadow_map_atlas_dsv.IsValid() ||
                !shadow_map_atlas_srv.IsValid() ||
                shadow_map_atlas_size.x != render_data.shadow_map_atlas_size.x ||
                shadow_map_atlas_size.y != render_data.shadow_map_atlas_size.y;

            if (recreate_shadowmap_atlas)
            {
                RemoveResourceDeferred(frame_context, shadow_map_atlas);
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
                    return;
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
                    return;
                }
                shadow_map_atlas_subresource_desc.type = RHISubresourceType::ShaderResource;
                if (!device->CreateSubresource(*shadow_map_atlas, shadow_map_atlas_subresource_desc, &shadow_map_atlas_srv))
                {
                    backlog::Post("failed to create shadow map atlas subresource", backlog::LogLevel::Error);
                    shadow_map_atlas = nullptr;
                    return;
                }

                shadow_map_atlas_size = render_data.shadow_map_atlas_size;
            }
        }

        frame_context.command_allocator->Reset();
        frame_context.command_list->Begin(*frame_context.command_allocator);
        frame_context.frame_upload_offset = 0;
        profiler::BeginFrameGPU(*device, current_frame_slot, *frame_context.command_list);

        {
            auto gpu_range = profiler::ScopedRangeGPU("Build Frame Context", *frame_context.command_list);
            if (!BuildFrameContext(view, frame_context))
            {
                return;
            }
        }

        const uint32 back_buffer_index = swapchain->GetCurrentBackBufferIndex();

        RHISubresourceBinding back_buffer_binding = {};
        back_buffer_binding.resource = back_buffer.get();
        back_buffer_binding.subresource = back_buffers_rtv[back_buffer_index];
        RHISubresourceBinding depth_buffer_binding = {};
        depth_buffer_binding.resource = depth_buffer.get();
        depth_buffer_binding.subresource = depth_buffer_dsv;
        Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };
        RHISubresourceBinding shader_frame_binding = {};
        shader_frame_binding.resource = shader_frame_buffer.get();
        shader_frame_binding.subresource = shader_frame_buffer_cbv;
        RHISubresourceBinding shader_camera_binding = {};
        shader_camera_binding.resource = shader_camera_buffer.get();
        shader_camera_binding.subresource = shader_camera_buffer_cbv;

        if (shader_library)
        {
            std::shared_ptr<RHIShader> current_ddgi_probe_update_shader = shader_library->GetShader(ShaderId::CSDDGIProbeUpdate);
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
        }
        debug_state.ddgi.probe_update_pipeline_ready = ddgi_probe_update_pipeline != nullptr;

        if (render_data.shader_environment_lighting.gi_mode == SHADER_ENVIRONMENT_GI_MODE_DDGI &&
            (render_data.shader_ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) != 0 &&
            render_data.shader_ddgi_volume.probe_counts.x > 0 &&
            render_data.shader_ddgi_volume.probe_counts.y > 0 &&
            render_data.shader_ddgi_volume.probe_counts.z > 0 &&
            ddgi_probe_update_pipeline &&
            ddgi_irradiance_texture &&
            ddgi_visibility_texture &&
            ddgi_irradiance_texture_srv.IsValid() &&
            ddgi_irradiance_texture_uav.IsValid() &&
            ddgi_visibility_texture_srv.IsValid() &&
            ddgi_visibility_texture_uav.IsValid())
        {
            debug_state.ddgi.dispatch_groups = {
                render_data.shader_ddgi_volume.probe_counts.x,
                render_data.shader_ddgi_volume.probe_counts.y,
                render_data.shader_ddgi_volume.probe_counts.z
            };
            auto gpu_range = profiler::ScopedRangeGPU("DDGI Probe Update", *frame_context.command_list);
            frame_context.command_list->BeginEvent("DDGI Probe Update");
            frame_context.command_list->TransitionResource(*ddgi_irradiance_texture, RHIResourceState::ShaderWrite);
            frame_context.command_list->TransitionResource(*ddgi_visibility_texture, RHIResourceState::ShaderWrite);
            frame_context.command_list->SetComputePipeline(*ddgi_probe_update_pipeline);
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Compute, 0, shader_frame_binding);
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Compute, 1, shader_camera_binding);
            frame_context.command_list->Dispatch(
                debug_state.ddgi.dispatch_groups.x,
                debug_state.ddgi.dispatch_groups.y,
                debug_state.ddgi.dispatch_groups.z);
            frame_context.command_list->UAVBarrier(*ddgi_irradiance_texture);
            frame_context.command_list->UAVBarrier(*ddgi_visibility_texture);
            frame_context.command_list->TransitionResource(*ddgi_irradiance_texture, RHIResourceState::ShaderRead);
            frame_context.command_list->TransitionResource(*ddgi_visibility_texture, RHIResourceState::ShaderRead);
            frame_context.command_list->EndEvent();
            debug_state.ddgi.probe_update_dispatched = true;
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

        {
            auto gpu_range = profiler::ScopedRangeGPU("Sky Pass", *frame_context.command_list);
            frame_context.command_list->BeginEvent("Sky Pass");
            frame_context.command_list->SetRenderTargets(color_targets, nullptr);
            frame_context.command_list->SetGraphicsPipeline(*shader_library->GetPipeline(RenderPassType::SkyPass).get());
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, shader_frame_binding);
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 1, shader_camera_binding);
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Pixel, 0, shader_frame_binding);
            frame_context.command_list->SetConstantBuffer(RHIShaderStage::Pixel, 1, shader_camera_binding);
            frame_context.command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
            frame_context.command_list->Draw(3, 1, 0, 0);
            frame_context.command_list->EndEvent();
        }

        if (shadow_map_atlas && shadow_map_atlas_dsv.IsValid() && !render_data.render_shadow_slices.empty())
        {
            auto gpu_range = profiler::ScopedRangeGPU("Shadow Pass", *frame_context.command_list);
            frame_context.command_list->BeginEvent("Fill Shadow Map Atlas");

            RHISubresourceBinding shadow_map_atlas_binding = {};
            shadow_map_atlas_binding.resource = shadow_map_atlas.get();
            shadow_map_atlas_binding.subresource = shadow_map_atlas_dsv;

            frame_context.command_list->TransitionResource(*shadow_map_atlas, RHIResourceState::DepthWrite);
            frame_context.command_list->ClearDepthStencil(shadow_map_atlas_binding, 0.0f, 0u);
            frame_context.command_list->SetRenderTargets({}, &shadow_map_atlas_binding);

            for (const auto& shadow_slice : render_data.render_shadow_slices)
            {
                if (!shadow_slice.HasShadowMapAtlasRect())
                {
                    continue;
                }

                ShaderCamera shadow_camera = {};
                shadow_camera.Init();

                shadow_camera.view_projection = shadow_slice.view_projection;

                if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shadow_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer))
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
                frame_context.command_list->SetViewport(shadow_viewport);

                RHIRect shadow_scissor = {};
                shadow_scissor.x = shadow_slice.shadow_map_atlas_rect.x;
                shadow_scissor.y = shadow_slice.shadow_map_atlas_rect.y;
                shadow_scissor.width = shadow_slice.shadow_map_atlas_rect.z;
                shadow_scissor.height = shadow_slice.shadow_map_atlas_rect.w;
                frame_context.command_list->SetScissor(shadow_scissor);

                DrawScene(view, frame_context, RenderPassType::ShadowPass, DrawScene_Opaque | DrawScene_ShadowCaster);
            }
            frame_context.command_list->TransitionResource(*shadow_map_atlas, RHIResourceState::ShaderRead);
            frame_context.command_list->EndEvent();

            frame_context.command_list->BeginEvent("Restore Render State");
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

            if (!UpdateDefaultBuffer(frame_context, *shader_camera_buffer, &shader_camera, sizeof(ShaderCamera), RHIResourceState::ConstantBuffer))
            {
                return;
            }

            frame_context.command_list->EndEvent();
        }

        frame_context.command_list->SetViewport(viewport);
        frame_context.command_list->SetScissor(scissor);

        // prepass
        {
            auto gpu_range = profiler::ScopedRangeGPU("Prepass", *frame_context.command_list);
            frame_context.command_list->BeginEvent("Prepass");
            frame_context.command_list->SetRenderTargets({}, & depth_buffer_binding);
            DrawScene(view, frame_context, RenderPassType::DepthPrepass, DrawScene_Opaque);
            frame_context.command_list->EndEvent();
        }
        
        // main pass
        {
            auto gpu_range = profiler::ScopedRangeGPU("Main Pass", *frame_context.command_list);
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

        profiler::EndFrameGPU(*frame_context.command_list);
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
        shader_instance_default_buffer_srv = {};
        shader_instance_default_buffer = nullptr;
        shader_geometry_default_buffer_srv = {};
        shader_geometry_default_buffer = nullptr;
        shader_material_default_buffer_srv = {};
        shader_material_default_buffer = nullptr;
        shader_light_default_buffer_srv = {};
        shader_light_default_buffer = nullptr;
        shader_shadow_cascade_default_buffer_srv = {};
        shader_shadow_cascade_default_buffer = nullptr;
        shader_bvh_node_default_buffer_srv = {};
        shader_bvh_node_default_buffer = nullptr;
        shader_bvh_primitive_default_buffer_srv = {};
        shader_bvh_primitive_default_buffer = nullptr;
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
        ddgi_visibility_texture_uav = {};
        ddgi_visibility_texture_srv = {};
        ddgi_visibility_texture = nullptr;
        ddgi_probe_update_pipeline = nullptr;
        ddgi_probe_update_shader = nullptr;
        debug_state = {};
        ddgi_probe_counts = { 0, 0, 0 };
        back_buffers_rtv = {};
        depth_buffer_dsv = {};
        depth_buffer = nullptr;
        device.reset();
    }
}
