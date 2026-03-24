#include "RenderingUtils.h"
#include "Backlog.h"
#include "ShaderCompiler.h"
#include "ShaderInterop_Utility.h"

namespace won::rendering::utils
{
    namespace
    {
        std::shared_ptr<RHIShader> texture_mipgen_shader = nullptr;
        std::shared_ptr<RHIPipeline> texture_mipgen_pipeline = nullptr;
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

        if (!texture_mipgen_shader)
        {
            resource::ShaderCompilerOptions compiler_options = {};
            compiler_options.shader_source_root_path = WONENGINE_SHADER_SOURCE_DIR;
            compiler_options.shader_bin_root_path = WONENGINE_SHADER_BIN_DIR;

            std::shared_ptr<resource::ShaderCompiler> shader_compiler = resource::CreateShaderCompiler(compiler_options);
            if (!shader_compiler)
            {
                backlog::Post("Failed to create mip generation shader compiler", backlog::LogLevel::Error);
                return false;
            }

            resource::ShaderCompileDesc compile_desc = {};
            compile_desc.stage = RHIShaderStage::Compute;
            compile_desc.source_file_name = "TextureMipGenCS.hlsl"; // TODO: as default manifest ?
            compile_desc.entry_point = "main";

            const resource::ShaderCompileResult compile_result = shader_compiler->Compile(compile_desc);
            if (compile_result.bytecode.empty())
            {
                backlog::Post("Failed to compile TextureMipGenCS.hlsl", backlog::LogLevel::Error);
                return false;
            }

            texture_mipgen_shader = std::make_shared<RHIShader>(RHIShaderStage::Compute, compile_result.bytecode.data(), compile_result.bytecode.size());
            if (!texture_mipgen_shader)
            {
                return false;
            }
            texture_mipgen_shader->SetName("TextureMipGenCS");
        }

        if (!texture_mipgen_pipeline)
        {
            RHIComputePipelineDesc pipeline_desc = {};
            pipeline_desc.compute_shader = texture_mipgen_shader.get();
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
}
