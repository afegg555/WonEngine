#include "RenderingUtils.h"
#include "Backlog.h"
#include "Mesh.h"
#include "Scene.h"
#include "ShaderLibrary.h"
#include "ShaderInterop_Utility.h"

namespace won::rendering::utils
{
    namespace
    {
        std::shared_ptr<RHIPipeline> texture_mipgen_pipeline = nullptr;

        template<typename T>
        void PackBufferSubresource(const Vector<T>& source, Vector<uint8>& packed_data, Size& out_offset, Size data_size, Size alignment, Size& current_offset)
        {
            if (data_size == 0)
            {
                out_offset = 0;
                return;
            }

            current_offset = math::align(current_offset, alignment);
            out_offset = current_offset;
            std::memcpy(packed_data.data() + out_offset, source.data(), data_size);
            current_offset += data_size;
        }

    }

    bool GenerateTextureMips(RHIDevice& device,
        RHIResource& texture_resource,
        const RHITextureDesc& desc)
    {
        const bool is_srgb = desc.format == RHIFormat::R8G8B8A8UnormSrgb;
        if ((desc.format != RHIFormat::R8G8B8A8Unorm && !is_srgb) ||
            desc.depth != 1 ||
            desc.array_layers != 1 ||
            desc.sample_count != 1 ||
            desc.mip_levels <= 1 ||
            !HasBindFlag(desc.bind_flags, RHIBindFlags::ShaderResource))
        {
            return true;
        }

        if (!texture_mipgen_pipeline)
        {
            resource::ShaderLibrary& shader_library = resource::GetShaderLibrary();
            std::shared_ptr<RHIShader> mipgen_shader = shader_library.GetShader(resource::ShaderId::CSTextureMipGen);
            if (!mipgen_shader)
            {
                backlog::Post("Failed to load TextureMipGenCS.hlsl", backlog::LogLevel::Error);
                return false;
            }

            RHIComputePipelineDesc pipeline_desc = {};
            pipeline_desc.compute_shader = mipgen_shader.get();
            texture_mipgen_pipeline = device.CreateComputePipeline(pipeline_desc);
            if (!texture_mipgen_pipeline)
            {
                backlog::Post("Failed to create texture mip generation pipeline", backlog::LogLevel::Error);
                return false;
            }

            texture_mipgen_pipeline->SetName("TextureMipGenPipeline");
        }

        std::shared_ptr<RHIContext> compute_context = device.GetContext(RHIQueueType::Compute);
        std::shared_ptr<RHICommandAllocator> compute_allocator = device.CreateCommandAllocator(RHIQueueType::Compute);
        std::shared_ptr<RHICommandList> compute_command_list = device.CreateCommandList(RHIQueueType::Compute);
        if (!compute_context || !compute_allocator || !compute_command_list)
        {
            return false;
        }

        compute_allocator->Reset();
        compute_command_list->Begin(*compute_allocator);
        compute_command_list->TransitionResource(texture_resource, RHIResourceState::ShaderRead);
        compute_command_list->SetComputePipeline(*texture_mipgen_pipeline);

        std::vector<RHISubresourceHandle> mip_srvs, mip_uavs;
        mip_srvs.resize(desc.mip_levels);
        mip_uavs.resize(desc.mip_levels);

        for (uint32 mip_index = 0; mip_index < desc.mip_levels; ++mip_index)
        {
            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.format = desc.format;
            srv_desc.first_slice = 0;
            srv_desc.slice_count = 1;
            srv_desc.first_mip = mip_index;
            srv_desc.mip_count = 1;
            if (!device.CreateSubresource(texture_resource, srv_desc, &mip_srvs[mip_index]))
            {
                return false;
            }

            RHISubresourceDesc uav_desc = {};
            uav_desc.type = RHISubresourceType::UnorderedAccess;
            uav_desc.format = RHIFormat::R8G8B8A8Unorm;
            uav_desc.first_slice = 0;
            uav_desc.slice_count = 1;
            uav_desc.first_mip = mip_index;
            uav_desc.mip_count = 1;
            if (!device.CreateSubresource(texture_resource, uav_desc, &mip_uavs[mip_index]))
            {
                return false;
            }
        }

        for (uint32 mip_index = 0; mip_index + 1 < desc.mip_levels; ++mip_index)
        {
            const uint32 destination_mip = mip_index + 1;
            const uint32 destination_width = (desc.width >> destination_mip) > 0 ? (desc.width >> destination_mip) : 1u;
            const uint32 destination_height = (desc.height >> destination_mip) > 0 ? (desc.height >> destination_mip) : 1u;

            compute_command_list->TransitionSubresource(texture_resource,
                RHIResourceState::ShaderRead, RHIResourceState::ShaderWrite,
                destination_mip, 1, 0, 1);

            TextureMipGenPushConstants push_constants = {};
            push_constants.source_mip_srv = static_cast<uint>(mip_srvs[mip_index].descriptor_index);
            push_constants.destination_mip_uav = static_cast<uint>(mip_uavs[destination_mip].descriptor_index);
            if (is_srgb)
            {
                push_constants.flags |= MIPGEN_FLAGS_IS_SRGB;
            }
            push_constants.destination_width = destination_width;
            push_constants.destination_height = destination_height;

            compute_command_list->PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
            compute_command_list->Dispatch((destination_width + DISPATCHBLOCKSIZE2D - 1) / DISPATCHBLOCKSIZE2D, (destination_height + DISPATCHBLOCKSIZE2D - 1) / DISPATCHBLOCKSIZE2D, 1u);
            compute_command_list->UAVBarrier(texture_resource);
            compute_command_list->TransitionSubresource(texture_resource,
                RHIResourceState::ShaderWrite, RHIResourceState::ShaderRead,
                destination_mip, 1, 0, 1);
        }

        compute_command_list->End();

        std::shared_ptr<RHIFence> compute_fence = device.CreateFence(0);
        if (!compute_fence)
        {
            return false;
        }

        const uint64 compute_fence_value = compute_context->Submit(*compute_command_list, compute_fence.get());
        //const uint64 compute_fence_value = compute_context->Submit(*compute_command_list, nullptr);
        if (compute_fence_value > 0)
        {
            compute_fence->Wait(compute_fence_value);
        }
        else
        {
            compute_context->WaitIdle();
        }

        return true;
    }

    bool CreateRenderData(RHIDevice& device, resource::Mesh& mesh)
    {
        if (mesh.render_data.IsValid())
        {
            return true;
        }

        if (!mesh.IsValid())
        {
            return false;
        }

        const Size positions_size = mesh.positions.size() * sizeof(float3);
        const Size colors_size = mesh.colors.size() * sizeof(float4);
        const Size normals_size = mesh.normals.size() * sizeof(float3);
        const Size tangents_size = mesh.tangents.size() * sizeof(float4);
        const Size texcoords_size = mesh.texcoords.size() * sizeof(float2);
        const Size indices_size = mesh.indices.size() * sizeof(uint32);
        Size total_size = 0;
        total_size = math::align(total_size, static_cast<Size>(sizeof(float3))) + positions_size;
        total_size = math::align(total_size, static_cast<Size>(sizeof(float4))) + colors_size;
        total_size = math::align(total_size, static_cast<Size>(sizeof(float3))) + normals_size;
        total_size = math::align(total_size, static_cast<Size>(sizeof(float4))) + tangents_size;
        total_size = math::align(total_size, static_cast<Size>(sizeof(float2))) + texcoords_size;
        total_size = math::align(total_size, static_cast<Size>(sizeof(uint32))) + indices_size;
        if (total_size == 0)
        {
            return false;
        }

        Vector<uint8> packed_data;
        packed_data.resize(total_size);

        resource::Mesh::RenderData new_render_data = {};
        Size offset = 0;

        Size positions_offset = 0;
        Size colors_offset = 0;
        Size normals_offset = 0;
        Size tangents_offset = 0;
        Size texcoords_offset = 0;
        Size indices_offset = 0;

        PackBufferSubresource(mesh.positions, packed_data, positions_offset, positions_size, sizeof(float3), offset);
        PackBufferSubresource(mesh.colors, packed_data, colors_offset, colors_size, sizeof(float4), offset);
        PackBufferSubresource(mesh.normals, packed_data, normals_offset, normals_size, sizeof(float3), offset);
        PackBufferSubresource(mesh.tangents, packed_data, tangents_offset, tangents_size, sizeof(float4), offset);
        PackBufferSubresource(mesh.texcoords, packed_data, texcoords_offset, texcoords_size, sizeof(float2), offset);
        PackBufferSubresource(mesh.indices, packed_data, indices_offset, indices_size, sizeof(uint32), offset);

        RHIBufferDesc buffer_desc = {};
        buffer_desc.size = total_size;
        buffer_desc.usage = RHIResourceUsage::Default;
        buffer_desc.bind_flags = RHIBindFlags::VertexBuffer | RHIBindFlags::IndexBuffer | RHIBindFlags::ShaderResource;
        new_render_data.buffer = device.CreateBuffer(buffer_desc, packed_data.data(), packed_data.size());
        if (!new_render_data.buffer)
        {
            return false;
        }

        auto create_subresource = [&](RHISubresourceType type, Size buffer_offset, Size buffer_size, Size buffer_stride, resource::Mesh::VBSubresource& out_subresource) -> bool
        {
            if (buffer_size == 0)
            {
                return true;
            }

            out_subresource.offset = static_cast<uint32>(buffer_offset);
            out_subresource.size = static_cast<uint32>(buffer_size);

            RHISubresourceDesc subresource_desc = {};
            subresource_desc.type = type;
            subresource_desc.buffer_offset = buffer_offset;
            subresource_desc.buffer_size = buffer_size;
            subresource_desc.buffer_stride = buffer_stride;
            return device.CreateSubresource(*new_render_data.buffer, subresource_desc, &out_subresource.handle);
        };

        if (!create_subresource(RHISubresourceType::ShaderResource, positions_offset, positions_size, sizeof(float3), new_render_data.positions))
        {
            return false;
        }
        if (!create_subresource(RHISubresourceType::ShaderResource, colors_offset, colors_size, sizeof(float4), new_render_data.colors))
        {
            return false;
        }
        if (!create_subresource(RHISubresourceType::ShaderResource, normals_offset, normals_size, sizeof(float3), new_render_data.normals))
        {
            return false;
        }
        if (!create_subresource(RHISubresourceType::ShaderResource, tangents_offset, tangents_size, sizeof(float4), new_render_data.tangents))
        {
            return false;
        }
        if (!create_subresource(RHISubresourceType::ShaderResource, texcoords_offset, texcoords_size, sizeof(float2), new_render_data.texcoords))
        {
            return false;
        }
        if (!create_subresource(RHISubresourceType::ShaderResource, indices_offset, indices_size, sizeof(uint32), new_render_data.indices))
        {
            return false;
        }

        mesh.render_data = std::move(new_render_data);
        return true;
    }

    bool CreateGPUBVH(RHIDevice& device, resource::Mesh& mesh)
    {
        mesh.ClearGPUBVH();
        return true;
    }

    void BuildGPUBVH(RHIDevice* device, ecs::Scene& scene)
    {
    }
}
