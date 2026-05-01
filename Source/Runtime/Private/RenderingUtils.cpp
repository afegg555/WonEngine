#include "RenderingUtils.h"
#include "Backlog.h"
#include "Mesh.h"
#include "ShaderLibrary.h"
#include "ShaderInterop_BVH.h"
#include "ShaderInterop_Utility.h"
#include "Timer.h"
#include <mutex>

namespace won::rendering::utils
{
    namespace
    {
        std::shared_ptr<RHIPipeline> texture_mipgen_pipeline = nullptr;
        Vector<std::weak_ptr<RHIResource>> pending_texture_mip_generation;
        std::mutex pending_texture_mip_generation_mutex;

        enum class GPUBVHBuildPipelineType : uint32
        {
            GeneratePrimitives,
            SortPrimitives,
            BuildNodes,
            ReduceBounds,
            Count
        };

        std::shared_ptr<RHIPipeline> gpu_bvh_build_pipelines[static_cast<uint32>(GPUBVHBuildPipelineType::Count)] = {};
        Vector<std::weak_ptr<resource::Mesh>> pending_gpu_bvh_build;
        std::mutex pending_gpu_bvh_build_mutex;

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

        bool FlushEnqueuedGPUBVHBuild(RHIDevice& device, RHICommandList& command_list, Vector<std::shared_ptr<RHIResource>>& scratch_resources)
        {
            Vector<std::weak_ptr<resource::Mesh>> pending_gpu_bvh_builds;
            {
                std::lock_guard<std::mutex> lock(pending_gpu_bvh_build_mutex);
                pending_gpu_bvh_builds.swap(pending_gpu_bvh_build);
            }

            bool succeeded = true;
            for (const std::weak_ptr<resource::Mesh>& pending_mesh : pending_gpu_bvh_builds)
            {
                std::shared_ptr<resource::Mesh> mesh_resource = pending_mesh.lock();
                if (!mesh_resource || !mesh_resource->IsValid() || !mesh_resource->gpu_bvh.dirty)
                {
                    continue;
                }

                resource::Mesh& mesh = *mesh_resource;
                // fast BVH Generation - Karras 2012 LBVH
                won::utils::Timer build_timer;
                mesh.ClearGPUBVH();
                if (!mesh.IsValid() || !CreateRenderData(device, mesh) || !mesh.render_data.IsValid())
                {
                    succeeded = false;
                    continue;
                }

                math::AABB mesh_bounds = {};
                mesh_bounds.Invalidate();
                uint32 primitive_count = 0;

                for (Size submesh_index = 0; submesh_index < mesh.submeshes.size(); ++submesh_index)
                {
                    const resource::Submesh& submesh = mesh.submeshes[submesh_index];
                    if (submesh.primitive_topology != resource::PrimitiveTopology::TriangleList)
                    {
                        continue;
                    }

                    if (submesh.first_index >= mesh.indices.size())
                    {
                        continue;
                    }
                    const uint32 available_index_count = (std::min)(submesh.index_count, static_cast<uint32>(mesh.indices.size()) - submesh.first_index);
                    const uint32 triangle_count = available_index_count / 3;
                    if (triangle_count == 0)
                    {
                        continue;
                    }

                    primitive_count += triangle_count;

                    if (submesh.local_bounds.IsValid())
                    {
                        mesh_bounds.Merge(submesh.local_bounds);
                    }
                }

                if (primitive_count == 0)
                {
                    mesh.gpu_bvh.dirty = false;
                    continue;
                }
                if (!mesh_bounds.IsValid())
                {
                    for (const float3& position : mesh.positions)
                    {
                        math::AABB vertex_bounds = {};
                        vertex_bounds.min = position;
                        vertex_bounds.max = position;
                        mesh_bounds.Merge(vertex_bounds);
                    }
                }
                if (!mesh_bounds.IsValid())
                {
                    mesh.gpu_bvh.dirty = false;
                    continue;
                }

                static const resource::ShaderId gpu_bvh_build_shader_ids[] =
                {
                    resource::ShaderId::CSGPUBVHBuildGeneratePrimitives,
                    resource::ShaderId::CSGPUBVHBuildSortPrimitives,
                    resource::ShaderId::CSGPUBVHBuildBuildNodes,
                    resource::ShaderId::CSGPUBVHBuildReduceBounds,
                };

                resource::ShaderLibrary& shader_library = resource::GetShaderLibrary();

                bool pipelines_ready = true;
                for (uint32 pipeline_index = 0; pipeline_index < static_cast<uint32>(GPUBVHBuildPipelineType::Count); ++pipeline_index)
                {
                    if (!gpu_bvh_build_pipelines[pipeline_index])
                    {
                        std::shared_ptr<RHIShader> build_shader = shader_library.GetShader(gpu_bvh_build_shader_ids[pipeline_index]);
                        if (!build_shader)
                        {
                            backlog::Post("Failed to get GPU BVH build shader", backlog::LogLevel::Error);
                            pipelines_ready = false;
                            break;
                        }

                        RHIComputePipelineDesc pipeline_desc = {};
                        pipeline_desc.compute_shader = build_shader.get();
                        gpu_bvh_build_pipelines[pipeline_index] = device.CreateComputePipeline(pipeline_desc);
                        if (!gpu_bvh_build_pipelines[pipeline_index])
                        {
                            backlog::Post("Failed to create GPU BVH build pipeline", backlog::LogLevel::Error);
                            pipelines_ready = false;
                            break;
                        }
                        gpu_bvh_build_pipelines[pipeline_index]->SetName("GPUBVHBuildPipeline");
                    }
                }
                if (!pipelines_ready)
                {
                    succeeded = false;
                    continue;
                }

                const uint32 sort_count = math::GetNextPowerOfTwo(primitive_count);
                const uint32 node_count = primitive_count * 2 - 1;
                // internal node: N - 1
                // leaf node: N
                // total node: 2N - 1

                resource::Mesh::GPUBVH gpu_bvh = {};
                std::shared_ptr<RHIResource> sort_buffer;
                std::shared_ptr<RHIResource> parent_buffer;
                std::shared_ptr<RHIResource> counter_buffer;
                RHISubresourceHandle sort_uav = {};
                RHISubresourceHandle parent_uav = {};
                RHISubresourceHandle counter_uav = {};

                const float3 bounds_extent = {
                    mesh_bounds.max.x - mesh_bounds.min.x,
                    mesh_bounds.max.y - mesh_bounds.min.y,
                    mesh_bounds.max.z - mesh_bounds.min.z
                };
                const float3 bounds_rcp_extent = {
                    1.0f / (std::max)(bounds_extent.x, FLT_EPSILON),
                    1.0f / (std::max)(bounds_extent.y, FLT_EPSILON),
                    1.0f / (std::max)(bounds_extent.z, FLT_EPSILON)
                };

                Vector<uint32> zero_counters;
                zero_counters.resize(node_count, 0);
                Vector<uint2> sort_keys;
                sort_keys.resize(sort_count);
                for (uint32 sort_index = 0; sort_index < sort_count; ++sort_index)
                {
                    sort_keys[sort_index] = { 0xFFFFFFFFu, sort_index };
                }

                auto create_structured_buffer = [&device](const char* buffer_name,
                    Size buffer_size,
                    Size stride,
                    RHIBindFlags bind_flags,
                    const void* data,
                    std::shared_ptr<RHIResource>& out_buffer,
                    RHISubresourceHandle* out_srv,
                    RHISubresourceHandle* out_uav) -> bool
                {
                    if (buffer_size == 0 || stride == 0)
                    {
                        return false;
                    }

                    RHIBufferDesc buffer_desc = {};
                    buffer_desc.size = buffer_size;
                    buffer_desc.usage = RHIResourceUsage::Default;
                    buffer_desc.bind_flags = bind_flags;
                    out_buffer = device.CreateBuffer(buffer_desc, data, data ? buffer_size : 0);
                    if (!out_buffer)
                    {
                        backlog::Post(String("failed to create ") + buffer_name, backlog::LogLevel::Error);
                        return false;
                    }
                    out_buffer->SetName(buffer_name);

                    if (out_srv)
                    {
                        RHISubresourceDesc srv_desc = {};
                        srv_desc.type = RHISubresourceType::ShaderResource;
                        srv_desc.buffer_offset = 0;
                        srv_desc.buffer_size = buffer_size;
                        srv_desc.buffer_stride = stride;
                        if (!device.CreateSubresource(*out_buffer, srv_desc, out_srv))
                        {
                            backlog::Post(String("failed to create ") + buffer_name + " SRV", backlog::LogLevel::Error);
                            return false;
                        }
                    }

                    if (out_uav)
                    {
                        RHISubresourceDesc uav_desc = {};
                        uav_desc.type = RHISubresourceType::UnorderedAccess;
                        uav_desc.buffer_offset = 0;
                        uav_desc.buffer_size = buffer_size;
                        uav_desc.buffer_stride = stride;
                        if (!device.CreateSubresource(*out_buffer, uav_desc, out_uav))
                        {
                            backlog::Post(String("failed to create ") + buffer_name + " UAV", backlog::LogLevel::Error);
                            return false;
                        }
                    }

                    return true;
                };

                if (!create_structured_buffer("Mesh GPU BVH Node Buffer",
                    node_count * sizeof(ShaderBVHNode), sizeof(ShaderBVHNode),
                    RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess, nullptr, gpu_bvh.node_buffer, &gpu_bvh.node_srv, &gpu_bvh.node_uav))
                {
                    succeeded = false;
                    continue;
                }
                if (!create_structured_buffer("Mesh GPU BVH Primitive Buffer",
                    sort_count * sizeof(ShaderBVHPrimitive), sizeof(ShaderBVHPrimitive),
                    RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess, nullptr, gpu_bvh.primitive_buffer, &gpu_bvh.primitive_srv, &gpu_bvh.primitive_uav))
                {
                    succeeded = false;
                    continue;
                }
                if (!create_structured_buffer("Mesh GPU BVH Sort Buffer",
                    sort_count * sizeof(uint2), sizeof(uint2),
                    RHIBindFlags::UnorderedAccess, sort_keys.data(), sort_buffer, nullptr, &sort_uav))
                {
                    succeeded = false;
                    continue;
                }
                if (!create_structured_buffer("Mesh GPU BVH Parent Buffer",
                    node_count * sizeof(uint32), sizeof(uint32),
                    RHIBindFlags::UnorderedAccess, nullptr, parent_buffer, nullptr, &parent_uav))
                {
                    succeeded = false;
                    continue;
                }
                if (!create_structured_buffer("Mesh GPU BVH Counter Buffer",
                    node_count * sizeof(uint32), sizeof(uint32),
                    RHIBindFlags::UnorderedAccess, zero_counters.data(), counter_buffer, nullptr, &counter_uav))
                {
                    succeeded = false;
                    continue;
                }
                scratch_resources.push_back(sort_buffer);
                scratch_resources.push_back(parent_buffer);
                scratch_resources.push_back(counter_buffer);
                command_list.TransitionResource(*mesh.render_data.buffer, RHIResourceState::ShaderRead);
                command_list.TransitionResource(*gpu_bvh.node_buffer, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*gpu_bvh.primitive_buffer, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*sort_buffer, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*parent_buffer, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*counter_buffer, RHIResourceState::ShaderWrite);

                command_list.SetComputePipeline(*gpu_bvh_build_pipelines[static_cast<uint32>(GPUBVHBuildPipelineType::GeneratePrimitives)]);
                command_list.SetShaderResource(RHIShaderStage::Compute, 0, { mesh.render_data.buffer.get(), mesh.render_data.positions.handle });
                command_list.SetShaderResource(RHIShaderStage::Compute, 1, { mesh.render_data.buffer.get(), mesh.render_data.indices.handle });
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 0, { gpu_bvh.primitive_buffer.get(), gpu_bvh.primitive_uav });
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 2, { sort_buffer.get(), sort_uav });
                uint32 primitive_offset = 0;
                for (Size submesh_index = 0; submesh_index < mesh.submeshes.size(); ++submesh_index)
                {
                    const resource::Submesh& submesh = mesh.submeshes[submesh_index];
                    if (submesh.primitive_topology != resource::PrimitiveTopology::TriangleList || submesh.first_index >= mesh.indices.size())
                    {
                        continue;
                    }

                    const uint32 available_index_count = (std::min)(submesh.index_count, static_cast<uint32>(mesh.indices.size()) - submesh.first_index);
                    const uint32 triangle_count = available_index_count / 3;
                    if (triangle_count == 0)
                    {
                        continue;
                    }

                    BVHGeneratePrimitivesPushConstants push_constants = {};
                    push_constants.first_index = submesh.first_index;
                    push_constants.primitive_offset = primitive_offset;
                    push_constants.triangle_count = triangle_count;
                    push_constants.submesh_index = static_cast<uint32>(submesh_index);
                    push_constants.material_slot = submesh.material_slot;
                    push_constants.bounds_min = mesh_bounds.min;
                    push_constants.bounds_rcp_extent = bounds_rcp_extent;
                    command_list.PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
                    command_list.Dispatch((triangle_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1);
                    primitive_offset += triangle_count;
                }
                command_list.UAVBarrier(*gpu_bvh.primitive_buffer);
                command_list.UAVBarrier(*sort_buffer);

                // bitonic sort
                if (sort_count > 1)
                {
                    command_list.SetComputePipeline(*gpu_bvh_build_pipelines[static_cast<uint32>(GPUBVHBuildPipelineType::SortPrimitives)]);
                    command_list.SetUnorderedAccess(RHIShaderStage::Compute, 0, { gpu_bvh.primitive_buffer.get(), gpu_bvh.primitive_uav });
                    command_list.SetUnorderedAccess(RHIShaderStage::Compute, 2, { sort_buffer.get(), sort_uav });
                    for (uint32 k = 2; k <= sort_count; k <<= 1)
                    {
                        for (uint32 j = k >> 1; j > 0; j >>= 1)
                        {
                            BVHSortPrimitivesPushConstants push_constants = {};
                            push_constants.sort_merge_size = k;
                            push_constants.sort_compare_stride = j;
                            command_list.PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
                            command_list.Dispatch((sort_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1);
                            command_list.UAVBarrier(*gpu_bvh.primitive_buffer);
                            command_list.UAVBarrier(*sort_buffer);
                        }
                    }
                }

                BVHPrimitiveCountPushConstants push_constants = {};
                push_constants.primitive_count = primitive_count;
                command_list.SetComputePipeline(*gpu_bvh_build_pipelines[static_cast<uint32>(GPUBVHBuildPipelineType::BuildNodes)]);
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 0, { gpu_bvh.primitive_buffer.get(), gpu_bvh.primitive_uav });
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 1, { gpu_bvh.node_buffer.get(), gpu_bvh.node_uav });
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 2, { sort_buffer.get(), sort_uav });
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 3, { parent_buffer.get(), parent_uav });
                command_list.PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
                command_list.Dispatch((primitive_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1);
                command_list.UAVBarrier(*gpu_bvh.node_buffer);
                command_list.UAVBarrier(*parent_buffer);

                if (primitive_count > 1)
                {
                    push_constants = {};
                    push_constants.primitive_count = primitive_count;
                    command_list.SetComputePipeline(*gpu_bvh_build_pipelines[static_cast<uint32>(GPUBVHBuildPipelineType::ReduceBounds)]);
                    command_list.SetUnorderedAccess(RHIShaderStage::Compute, 1, { gpu_bvh.node_buffer.get(), gpu_bvh.node_uav });
                    command_list.SetUnorderedAccess(RHIShaderStage::Compute, 3, { parent_buffer.get(), parent_uav });
                    command_list.SetUnorderedAccess(RHIShaderStage::Compute, 4, { counter_buffer.get(), counter_uav });
                    command_list.PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
                    command_list.Dispatch((primitive_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1);
                    command_list.UAVBarrier(*gpu_bvh.node_buffer);
                }

                command_list.TransitionResource(*gpu_bvh.node_buffer, RHIResourceState::ShaderRead);
                command_list.TransitionResource(*gpu_bvh.primitive_buffer, RHIResourceState::ShaderRead);
                command_list.TransitionResource(*mesh.render_data.buffer, RHIResourceState::Undefined);

                gpu_bvh.node_count = node_count;
                gpu_bvh.primitive_count = primitive_count;
                gpu_bvh.dirty = false;
                mesh.gpu_bvh = std::move(gpu_bvh);
                backlog::Post("GPU BVH built: mesh=" + std::to_string(reinterpret_cast<uintptr_t>(&mesh)) +
                    ", primitives=" + std::to_string(primitive_count) +
                    ", nodes=" + std::to_string(node_count) +
                    ", time=" + std::to_string(build_timer.ElapsedMilliSeconds()) + " ms");
            }
            return succeeded;
        }

        bool FlushEnqueuedTextureMipGeneration(RHIDevice& device, RHICommandList& command_list)
        {
            Vector<std::weak_ptr<RHIResource>> pending;
            {
                std::lock_guard<std::mutex> lock(pending_texture_mip_generation_mutex);
                pending.swap(pending_texture_mip_generation);
            }

            bool succeeded = true;
            for (const std::weak_ptr<RHIResource>& pending_resource : pending)
            {
                std::shared_ptr<RHIResource> texture_resource = pending_resource.lock();
                if (!texture_resource)
                {
                    continue;
                }

                const RHIResourceDesc& resource_desc = texture_resource->GetDesc();
                const RHITextureDesc& desc = resource_desc.texture_desc;
                const bool is_srgb = desc.format == RHIFormat::R8G8B8A8UnormSrgb;
                const bool is_supported_format = desc.format == RHIFormat::R8G8B8A8Unorm || is_srgb;
                if (resource_desc.type != RHIResourceType::Texture2D ||
                    !is_supported_format ||
                    desc.depth != 1 ||
                    desc.array_layers != 1 ||
                    desc.sample_count != 1 ||
                    desc.mip_levels <= 1 ||
                    !HasBindFlag(desc.bind_flags, RHIBindFlags::ShaderResource))
                {
                    continue;
                }

                if (!texture_mipgen_pipeline)
                {
                    resource::ShaderLibrary& shader_library = resource::GetShaderLibrary();
                    std::shared_ptr<RHIShader> mipgen_shader = shader_library.GetShader(resource::ShaderId::CSTextureMipGen);
                    if (!mipgen_shader)
                    {
                        backlog::Post("Failed to load TextureMipGenCS.hlsl", backlog::LogLevel::Error);
                        succeeded = false;
                        continue;
                    }

                    RHIComputePipelineDesc pipeline_desc = {};
                    pipeline_desc.compute_shader = mipgen_shader.get();
                    texture_mipgen_pipeline = device.CreateComputePipeline(pipeline_desc);
                    if (!texture_mipgen_pipeline)
                    {
                        backlog::Post("Failed to create texture mip generation pipeline", backlog::LogLevel::Error);
                        succeeded = false;
                        continue;
                    }
                    texture_mipgen_pipeline->SetName("TextureMipGenPipeline");
                }

                command_list.TransitionResource(*texture_resource, RHIResourceState::ShaderRead);
                command_list.SetComputePipeline(*texture_mipgen_pipeline);

                std::vector<RHISubresourceHandle> mip_srvs, mip_uavs;
                mip_srvs.resize(desc.mip_levels);
                mip_uavs.resize(desc.mip_levels);

                bool texture_mipgen_ready = true;
                for (uint32 mip_index = 0; mip_index < desc.mip_levels; ++mip_index)
                {
                    RHISubresourceDesc srv_desc = {};
                    srv_desc.type = RHISubresourceType::ShaderResource;
                    srv_desc.format = desc.format;
                    srv_desc.first_slice = 0;
                    srv_desc.slice_count = 1;
                    srv_desc.first_mip = mip_index;
                    srv_desc.mip_count = 1;
                    if (!device.CreateSubresource(*texture_resource, srv_desc, &mip_srvs[mip_index]))
                    {
                        succeeded = false;
                        texture_mipgen_ready = false;
                        break;
                    }

                    RHISubresourceDesc uav_desc = {};
                    uav_desc.type = RHISubresourceType::UnorderedAccess;
                    uav_desc.format = RHIFormat::R8G8B8A8Unorm;
                    uav_desc.first_slice = 0;
                    uav_desc.slice_count = 1;
                    uav_desc.first_mip = mip_index;
                    uav_desc.mip_count = 1;
                    if (!device.CreateSubresource(*texture_resource, uav_desc, &mip_uavs[mip_index]))
                    {
                        succeeded = false;
                        texture_mipgen_ready = false;
                        break;
                    }
                }
                if (!texture_mipgen_ready)
                {
                    continue;
                }

                for (uint32 mip_index = 0; mip_index + 1 < desc.mip_levels; ++mip_index)
                {
                    const uint32 destination_mip = mip_index + 1;
                    const uint32 destination_width = (desc.width >> destination_mip) > 0 ? (desc.width >> destination_mip) : 1u;
                    const uint32 destination_height = (desc.height >> destination_mip) > 0 ? (desc.height >> destination_mip) : 1u;

                    command_list.TransitionSubresource(*texture_resource,
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

                    command_list.PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
                    command_list.Dispatch((destination_width + DISPATCHBLOCKSIZE2D - 1) / DISPATCHBLOCKSIZE2D, (destination_height + DISPATCHBLOCKSIZE2D - 1) / DISPATCHBLOCKSIZE2D, 1u);
                    command_list.UAVBarrier(*texture_resource);
                    command_list.TransitionSubresource(*texture_resource,
                        RHIResourceState::ShaderWrite, RHIResourceState::ShaderRead,
                        destination_mip, 1, 0, 1);
                }
            }

            return succeeded;
        }

    }

    void EnqueueTextureMipGeneration(const std::shared_ptr<RHIResource>& texture_resource)
    {
        if (!texture_resource)
        {
            return;
        }

        const RHIResourceDesc& resource_desc = texture_resource->GetDesc();
        const RHITextureDesc& desc = resource_desc.texture_desc;
        const bool is_supported_format = desc.format == RHIFormat::R8G8B8A8Unorm || desc.format == RHIFormat::R8G8B8A8UnormSrgb;
        if (resource_desc.type != RHIResourceType::Texture2D ||
            !is_supported_format ||
            desc.depth != 1 ||
            desc.array_layers != 1 ||
            desc.sample_count != 1 ||
            desc.mip_levels <= 1 ||
            !HasBindFlag(desc.bind_flags, RHIBindFlags::ShaderResource))
        {
            return;
        }

        std::lock_guard<std::mutex> lock(pending_texture_mip_generation_mutex);
        pending_texture_mip_generation.push_back(texture_resource);
    }

    void EnqueueGPUBVHBuild(const std::shared_ptr<resource::Mesh>& mesh)
    {
        if (!mesh || !mesh->IsValid() || !mesh->gpu_bvh.dirty)
        {
            return;
        }

        std::lock_guard<std::mutex> lock(pending_gpu_bvh_build_mutex);
        for (const std::weak_ptr<resource::Mesh>& pending_mesh : pending_gpu_bvh_build)
        {
            if (pending_mesh.lock().get() == mesh.get())
            {
                return;
            }
        }
        pending_gpu_bvh_build.push_back(mesh);
    }

    bool FlushEnqueuedRenderingWork(RHIDevice& device, RHICommandList& command_list, Vector<std::shared_ptr<RHIResource>>& scratch_resources)
    {
        bool succeeded = true;
        succeeded &= FlushEnqueuedGPUBVHBuild(device, command_list, scratch_resources);
        succeeded &= FlushEnqueuedTextureMipGeneration(device, command_list);
        command_list.End();
        return succeeded;
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

}
