#include "RenderingUtils.h"
#include "Backlog.h"
#include "Font.h"
#include "Image.h"
#include "Mesh.h"
#include "Material.h"
#include "GPUScene.h"
#include "Impostor.h"
#include "MathUtils.h"
#include "ShaderLibrary.h"
#include "ShaderInterop_BVH.h"
#include "ShaderInterop_Utility.h"
#include "Timer.h"
#include "Renderer.h"
#include "SceneComponents.h"
#include <mutex>

namespace won::rendering::utils
{
    namespace
    {
        std::unique_ptr<RHIPipeline> texture_mipgen_pipeline = nullptr;
        std::unique_ptr<RHIPipeline> texture_bc_compress_pipelines[4] = {};
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

        std::unique_ptr<RHIPipeline> gpu_bvh_build_pipelines[static_cast<uint32>(GPUBVHBuildPipelineType::Count)] = {};
        Vector<std::weak_ptr<resource::Mesh>> pending_gpu_bvh_build;
        std::mutex pending_gpu_bvh_build_mutex;

        struct PendingImageUpload
        {
            std::weak_ptr<resource::Image> image;
            RHIFormat format;
        };
        Vector<std::weak_ptr<resource::Mesh>> pending_mesh_upload;
        Vector<std::weak_ptr<resource::Font>> pending_font_upload;
        Vector<PendingImageUpload> pending_image_upload;
        std::mutex pending_resource_upload_mutex;

        Vector<std::weak_ptr<resource::Mesh>> pending_vertex_stream_update;
        std::mutex pending_vertex_stream_update_mutex;
        Vector<std::shared_ptr<resource::Mesh>> pending_mesh_release;
        std::mutex pending_mesh_release_mutex;

        template<typename T>
        void PackBufferSubresource(const Vector<T>& source, Vector<uint8>& packed_data, Size& out_offset, Size data_size, Size alignment, Size& current_offset, Size slot_count = 1)
        {
            if (data_size == 0)
            {
                out_offset = 0;
                return;
            }

            current_offset = math::Align(current_offset, alignment);
            out_offset = current_offset;
            const Size slot_size = data_size / slot_count;
            for (Size slot = 0; slot < slot_count; ++slot)
            {
                std::memcpy(packed_data.data() + out_offset + slot * slot_size, source.data(), slot_size);
            }
            current_offset += data_size;
        }

        bool CreateBufferView(RHIDevice& device, RHIResource& buffer, RHISubresourceType type, Size offset, Size size, Size stride, RHISubresourceHandle& out_handle)
        {
            RHISubresourceDesc view_desc = {};
            view_desc.type = type;
            view_desc.buffer_offset = offset;
            view_desc.buffer_size = size;
            view_desc.buffer_stride = stride;
            return device.CreateSubresource(buffer, view_desc, &out_handle);
        }

        std::unique_ptr<RHIResource> CreateStructuredBuffer(RHIDevice& device, const char* name, const void* data, Size size, Size stride, RHIBindFlags bind_flags, RHISubresourceHandle* out_srv, RHISubresourceHandle* out_uav)
        {
            if (size == 0 || stride == 0)
            {
                return nullptr;
            }
            RHIBufferDesc desc = {};
            desc.size = size;
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = bind_flags;
            std::unique_ptr<RHIResource> buffer = device.CreateBuffer(desc, data, data ? size : 0);
            if (!buffer)
            {
                backlog::Post(String("failed to create ") + name, backlog::LogLevel::Error);
                return nullptr;
            }
            buffer->SetName(name);
            if (out_srv && !CreateBufferView(device, *buffer, RHISubresourceType::ShaderResource, 0, size, stride, *out_srv))
            {
                backlog::Post(String("failed to create ") + name + " SRV", backlog::LogLevel::Error);
                return nullptr;
            }
            if (out_uav && !CreateBufferView(device, *buffer, RHISubresourceType::UnorderedAccess, 0, size, stride, *out_uav))
            {
                backlog::Post(String("failed to create ") + name + " UAV", backlog::LogLevel::Error);
                return nullptr;
            }
            return buffer;
        }

        std::unique_ptr<RHIResource> CreateConstantBuffer(RHIDevice& device, const void* data, Size size, RHISubresourceHandle& out_cbv)
        {
            RHIBufferDesc desc = {};
            desc.size = size;
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = RHIBindFlags::ConstantBuffer;
            std::unique_ptr<RHIResource> buffer = device.CreateBuffer(desc, data, size);
            if (!buffer)
            {
                return nullptr;
            }
            if (!CreateBufferView(device, *buffer, RHISubresourceType::ConstantBuffer, 0, size, 0, out_cbv))
            {
                return nullptr;
            }
            return buffer;
        }

        std::unique_ptr<RHIResource> CreateRenderTargetTexture(RHIDevice& device, uint32 width, uint32 height, RHIFormat format, RHIBindFlags bind_flags, RHISubresourceType view_type, RHISubresourceHandle& out_view)
        {
            RHITextureDesc desc = {};
            desc.width = width;
            desc.height = height;
            desc.format = format;
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = bind_flags;
            std::unique_ptr<RHIResource> texture = device.CreateTexture(desc);
            if (!texture)
            {
                return nullptr;
            }
            RHISubresourceDesc view_desc = {};
            view_desc.type = view_type;
            view_desc.format = format;
            if (!device.CreateSubresource(*texture, view_desc, &out_view))
            {
                return nullptr;
            }
            return texture;
        }

        bool ReadbackTexture(RHIDevice& device, RHIResource& texture, RHIResourceState current_state,
            Vector<uint8>& out_pixels, uint32& out_row_pitch, uint32& out_rows)
        {
            Size total_size = 0;
            if (!device.GetTextureCopyFootprint(texture, total_size, out_row_pitch, out_rows) || total_size == 0)
            {
                return false;
            }

            RHIBufferDesc readback_desc = {};
            readback_desc.size = total_size;
            readback_desc.usage = RHIResourceUsage::Readback;
            readback_desc.bind_flags = RHIBindFlags::None;
            std::unique_ptr<RHIResource> readback_buffer = device.CreateBuffer(readback_desc);
            if (!readback_buffer || !readback_buffer->GetMappedData())
            {
                return false;
            }

            RHIContext* context = device.GetContext(RHIQueueType::Graphics);
            std::unique_ptr<RHICommandAllocator> command_allocator = device.CreateCommandAllocator(RHIQueueType::Graphics);
            std::unique_ptr<RHICommandList> command_list = device.CreateCommandList(RHIQueueType::Graphics);
            if (!context || !command_allocator || !command_list)
            {
                return false;
            }

            command_allocator->Reset();
            command_list->Begin(*command_allocator);
            command_list->TransitionResource(texture, current_state, RHIResourceState::CopySource);
            command_list->CopyTextureToBuffer(*readback_buffer, texture);
            command_list->TransitionResource(texture, RHIResourceState::CopySource, current_state);
            command_list->End();

            std::unique_ptr<RHIFence> fence = device.CreateFence(0);
            const uint64 fence_value = context->Submit(*command_list, fence.get());
            if (fence_value > 0)
            {
                fence->Wait(fence_value);
            }
            else
            {
                context->WaitIdle();
            }

            out_pixels.resize(total_size);
            std::memcpy(out_pixels.data(), readback_buffer->GetMappedData(), total_size);
            return true;
        }

        std::shared_ptr<resource::Image> ReadbackTextureToImage(RHIDevice& device, RHIResource& texture, RHIResourceState current_state,
            uint32 dimension, uint32 bytes_per_pixel, RHIFormat format, int32 channels)
        {
            Vector<uint8> raw;
            uint32 row_pitch = 0;
            uint32 rows = 0;
            if (!ReadbackTexture(device, texture, current_state, raw, row_pitch, rows))
            {
                return nullptr;
            }
            auto image = std::make_shared<resource::Image>();
            image->width = static_cast<int32>(dimension);
            image->height = static_cast<int32>(dimension);
            image->channels = channels;
            image->format = format;
            const uint32 tight_pitch = dimension * bytes_per_pixel;
            image->pixels.resize(static_cast<Size>(tight_pitch) * dimension);
            for (uint32 row = 0; row < dimension; ++row)
            {
                std::memcpy(image->pixels.data() + static_cast<Size>(row) * tight_pitch, raw.data() + static_cast<Size>(row) * row_pitch, tight_pitch);
            }
            return image;
        }

        bool GenerateTextureMips(RHIDevice& device, Renderer& renderer, RHICommandList& command_list, RHIResource& texture_resource, Vector<RHISubresourceHandle>* out_mip_srvs)
        {
            if (out_mip_srvs)
            {
                out_mip_srvs->clear();
            }

            const RHIResourceDesc& resource_desc = texture_resource.GetDesc();
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
                return true;
            }

            if (!texture_mipgen_pipeline)
            {
                RHIShader* mipgen_shader = renderer.GetShader(resource::ShaderId::CSTextureMipGen);
                if (!mipgen_shader)
                {
                    backlog::Post("Failed to load TextureMipGenCS.hlsl", backlog::LogLevel::Error);
                    return false;
                }

                RHIComputePipelineDesc pipeline_desc = {};
                pipeline_desc.compute_shader = mipgen_shader;
                texture_mipgen_pipeline = device.CreateComputePipeline(pipeline_desc);
                if (!texture_mipgen_pipeline)
                {
                    backlog::Post("Failed to create texture mip generation pipeline", backlog::LogLevel::Error);
                    return false;
                }
                texture_mipgen_pipeline->SetName("TextureMipGenPipeline");
            }

            command_list.TransitionResource(texture_resource, RHIResourceState::Undefined, RHIResourceState::ShaderRead);
            command_list.SetComputePipeline(*texture_mipgen_pipeline);

            Vector<RHISubresourceHandle> mip_srvs;
            Vector<RHISubresourceHandle> mip_uavs;
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

            if (out_mip_srvs)
            {
                *out_mip_srvs = mip_srvs;
            }

            for (uint32 mip_index = 0; mip_index + 1 < desc.mip_levels; ++mip_index)
            {
                const uint32 destination_mip = mip_index + 1;
                const uint32 destination_width = (desc.width >> destination_mip) > 0 ? (desc.width >> destination_mip) : 1u;
                const uint32 destination_height = (desc.height >> destination_mip) > 0 ? (desc.height >> destination_mip) : 1u;

                command_list.TransitionSubresource(texture_resource,
                    RHIResourceState::ShaderRead, RHIResourceState::ShaderWrite,
                    destination_mip, 1, 0, 1);

                TextureMipGenPushConstants push_constants = {};
                push_constants.source_mip_srv = static_cast<uint>(mip_srvs[mip_index].descriptor_index);
                push_constants.destination_mip_uav = static_cast<uint>(mip_uavs[destination_mip].descriptor_index);
                if (is_srgb)
                {
                    push_constants.flags |= MIPGEN_FLAGS_IS_SRGB;
                }

                command_list.PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
                command_list.Dispatch((destination_width + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D, (destination_height + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D, 1u);
                command_list.UAVBarrier(texture_resource);
                command_list.TransitionSubresource(texture_resource,
                    RHIResourceState::ShaderWrite, RHIResourceState::ShaderRead,
                    destination_mip, 1, 0, 1);
            }

            return true;
        }

        bool FlushEnqueuedGPUBVHBuild(RHIDevice& device, Renderer& renderer, RHICommandList& command_list, Vector<std::unique_ptr<RHIResource>>& scratch_resources)
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

                bool pipelines_ready = true;
                for (uint32 pipeline_index = 0; pipeline_index < static_cast<uint32>(GPUBVHBuildPipelineType::Count); ++pipeline_index)
                {
                    if (!gpu_bvh_build_pipelines[pipeline_index])
                    {
                        RHIShader* build_shader = renderer.GetShader(gpu_bvh_build_shader_ids[pipeline_index]);
                        if (!build_shader)
                        {
                            backlog::Post("Failed to get GPU BVH build shader", backlog::LogLevel::Error);
                            pipelines_ready = false;
                            break;
                        }

                        RHIComputePipelineDesc pipeline_desc = {};
                        pipeline_desc.compute_shader = build_shader;
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
                std::unique_ptr<RHIResource> sort_buffer;
                std::unique_ptr<RHIResource> parent_buffer;
                std::unique_ptr<RHIResource> counter_buffer;
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

                gpu_bvh.node_buffer = CreateStructuredBuffer(device, "Mesh GPU BVH Node Buffer",
                    nullptr, node_count * sizeof(ShaderBVHNode), sizeof(ShaderBVHNode),
                    RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess, &gpu_bvh.node_srv, &gpu_bvh.node_uav);
                if (!gpu_bvh.node_buffer)
                {
                    succeeded = false;
                    continue;
                }
                gpu_bvh.primitive_buffer = CreateStructuredBuffer(device, "Mesh GPU BVH Primitive Buffer",
                    nullptr, sort_count * sizeof(ShaderBVHPrimitive), sizeof(ShaderBVHPrimitive),
                    RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess, &gpu_bvh.primitive_srv, &gpu_bvh.primitive_uav);
                if (!gpu_bvh.primitive_buffer)
                {
                    succeeded = false;
                    continue;
                }
                sort_buffer = CreateStructuredBuffer(device, "Mesh GPU BVH Sort Buffer",
                    sort_keys.data(), sort_count * sizeof(uint2), sizeof(uint2),
                    RHIBindFlags::UnorderedAccess, nullptr, &sort_uav);
                if (!sort_buffer)
                {
                    succeeded = false;
                    continue;
                }
                parent_buffer = CreateStructuredBuffer(device, "Mesh GPU BVH Parent Buffer",
                    nullptr, node_count * sizeof(uint32), sizeof(uint32),
                    RHIBindFlags::UnorderedAccess, nullptr, &parent_uav);
                if (!parent_buffer)
                {
                    succeeded = false;
                    continue;
                }
                counter_buffer = CreateStructuredBuffer(device, "Mesh GPU BVH Counter Buffer",
                    zero_counters.data(), node_count * sizeof(uint32), sizeof(uint32),
                    RHIBindFlags::UnorderedAccess, nullptr, &counter_uav);
                if (!counter_buffer)
                {
                    succeeded = false;
                    continue;
                }
                command_list.TransitionResource(*mesh.render_data.buffer, RHIResourceState::Undefined, RHIResourceState::ShaderRead);
                command_list.TransitionResource(*gpu_bvh.node_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*gpu_bvh.primitive_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*sort_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*parent_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
                command_list.TransitionResource(*counter_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);

                Vector<ShaderBVHBuildSubmesh> build_submeshes;
                build_submeshes.reserve(mesh.submeshes.size());
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

                    ShaderBVHBuildSubmesh build_submesh = {};
                    build_submesh.bounds_min = mesh_bounds.min;
                    build_submesh.bounds_rcp_extent = bounds_rcp_extent;
                    build_submesh.first_index = submesh.first_index;
                    build_submesh.primitive_offset = primitive_offset;
                    build_submesh.triangle_count = triangle_count;
                    build_submesh.submesh_index = static_cast<uint32>(submesh_index);
                    build_submesh.material_slot = submesh.material_slot;
                    build_submeshes.push_back(build_submesh);
                    primitive_offset += triangle_count;
                }

                RHISubresourceHandle build_submesh_srv = {};
                std::unique_ptr<RHIResource> build_submesh_buffer = CreateStructuredBuffer(device, "Mesh GPU BVH Build Submesh Buffer",
                    build_submeshes.data(), build_submeshes.size() * sizeof(ShaderBVHBuildSubmesh), sizeof(ShaderBVHBuildSubmesh),
                    RHIBindFlags::ShaderResource, &build_submesh_srv, nullptr);
                if (!build_submesh_buffer)
                {
                    succeeded = false;
                    continue;
                }

                command_list.SetComputePipeline(*gpu_bvh_build_pipelines[static_cast<uint32>(GPUBVHBuildPipelineType::GeneratePrimitives)]);
                command_list.SetShaderResource(RHIShaderStage::Compute, 0, { mesh.render_data.buffer.get(), mesh.render_data.positions.srv });
                command_list.SetShaderResource(RHIShaderStage::Compute, 1, { mesh.render_data.buffer.get(), mesh.render_data.indices.srv });
                command_list.SetShaderResource(RHIShaderStage::Compute, 2, { build_submesh_buffer.get(), build_submesh_srv });
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 0, { gpu_bvh.primitive_buffer.get(), gpu_bvh.primitive_uav });
                command_list.SetUnorderedAccess(RHIShaderStage::Compute, 2, { sort_buffer.get(), sort_uav });
                for (Size build_index = 0; build_index < build_submeshes.size(); ++build_index)
                {
                    BVHGeneratePrimitivesPushConstants push_constants = {};
                    push_constants.build_submesh_index = static_cast<uint32>(build_index);
                    command_list.PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
                    command_list.Dispatch((build_submeshes[build_index].triangle_count + BVH_BUILDER_GROUPSIZE - 1) / BVH_BUILDER_GROUPSIZE, 1, 1);
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

                scratch_resources.push_back(std::move(build_submesh_buffer));
                scratch_resources.push_back(std::move(sort_buffer));
                scratch_resources.push_back(std::move(parent_buffer));
                scratch_resources.push_back(std::move(counter_buffer));

                command_list.TransitionResource(*gpu_bvh.node_buffer, RHIResourceState::ShaderWrite, RHIResourceState::Undefined);
                command_list.TransitionResource(*gpu_bvh.primitive_buffer, RHIResourceState::ShaderWrite, RHIResourceState::Undefined);
                command_list.TransitionResource(*mesh.render_data.buffer, RHIResourceState::ShaderRead, RHIResourceState::Undefined);

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

        bool FlushEnqueuedTextureMipGeneration(RHIDevice& device, Renderer& renderer, RHICommandList& command_list)
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

                if (!GenerateTextureMips(device, renderer, command_list, *texture_resource, nullptr))
                {
                    succeeded = false;
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

    bool FlushEnqueuedRenderingWork(RHIDevice& device, Renderer& renderer, RHICommandList& command_list, Vector<std::unique_ptr<RHIResource>>& scratch_resources)
    {
        bool succeeded = true;
        succeeded &= FlushEnqueuedGPUBVHBuild(device, renderer, command_list, scratch_resources);
        succeeded &= FlushEnqueuedTextureMipGeneration(device, renderer, command_list);
        command_list.End();
        return succeeded;
    }

    void EnqueueResourceUpload(const std::shared_ptr<resource::Mesh>& mesh)
    {
        if (!mesh || mesh->render_data.IsValid())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pending_resource_upload_mutex);
        for (const std::weak_ptr<resource::Mesh>& pending : pending_mesh_upload)
        {
            if (pending.lock().get() == mesh.get())
            {
                return;
            }
        }
        pending_mesh_upload.push_back(mesh);
    }

    void EnqueueVertexStreamUpdate(const std::shared_ptr<resource::Mesh>& mesh)
    {
        if (!mesh || !mesh->render_data.IsValid())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pending_vertex_stream_update_mutex);
        for (const std::weak_ptr<resource::Mesh>& pending : pending_vertex_stream_update)
        {
            if (pending.lock().get() == mesh.get())
            {
                return;
            }
        }
        pending_vertex_stream_update.push_back(mesh);
    }

    void TakeEnqueuedVertexStreamUpdates(Vector<std::shared_ptr<resource::Mesh>>& out_meshes)
    {
        out_meshes.clear();
        std::lock_guard<std::mutex> lock(pending_vertex_stream_update_mutex);
        out_meshes.reserve(pending_vertex_stream_update.size());
        for (const std::weak_ptr<resource::Mesh>& pending : pending_vertex_stream_update)
        {
            if (std::shared_ptr<resource::Mesh> mesh = pending.lock())
            {
                out_meshes.push_back(std::move(mesh));
            }
        }
        pending_vertex_stream_update.clear();
    }

    void EnqueueMeshRelease(const std::shared_ptr<resource::Mesh>& mesh)
    {
        if (!mesh || !mesh->render_data.IsValid())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pending_mesh_release_mutex);
        pending_mesh_release.push_back(mesh);
    }

    void TakeEnqueuedMeshReleases(Vector<std::shared_ptr<resource::Mesh>>& out_meshes)
    {
        out_meshes.clear();
        std::lock_guard<std::mutex> lock(pending_mesh_release_mutex);
        out_meshes.swap(pending_mesh_release);
    }

    void EnqueueResourceUpload(const std::shared_ptr<resource::Font>& font)
    {
        if (!font)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pending_resource_upload_mutex);
        for (const std::weak_ptr<resource::Font>& pending : pending_font_upload)
        {
            if (pending.lock().get() == font.get())
            {
                return;
            }
        }
        pending_font_upload.push_back(font);
    }

    void EnqueueResourceUpload(const std::shared_ptr<resource::Image>& image, RHIFormat format)
    {
        if (!image || image->render_data.IsValid())
        {
            return;
        }
        std::lock_guard<std::mutex> lock(pending_resource_upload_mutex);
        for (const PendingImageUpload& pending : pending_image_upload)
        {
            if (pending.image.lock().get() == image.get())
            {
                return;
            }
        }
        pending_image_upload.push_back({ image, format });
    }

    bool FlushEnqueuedResourceUploads(RHIDevice& device, uint32 max_uploads, uint64* out_completed_component_mask)
    {
        ecs::ComponentMask completed_component_mask = ecs::none_component_mask;

        Vector<std::weak_ptr<resource::Mesh>> meshes;
        Vector<std::weak_ptr<resource::Font>> fonts;
        Vector<PendingImageUpload> images;
        bool queue_empty = false;
        {
            std::lock_guard<std::mutex> lock(pending_resource_upload_mutex);
            if (max_uploads == 0)
            {
                meshes.swap(pending_mesh_upload);
                fonts.swap(pending_font_upload);
                images.swap(pending_image_upload);
            }
            else
            {
                uint32 remaining = max_uploads;
                auto take = [&remaining](auto& from, auto& to)
                {
                    while (remaining > 0 && !from.empty())
                    {
                        to.push_back(std::move(from.back()));
                        from.pop_back();
                        --remaining;
                    }
                };
                take(pending_mesh_upload, meshes);
                take(pending_font_upload, fonts);
                take(pending_image_upload, images);
            }
            queue_empty = pending_mesh_upload.empty() && pending_font_upload.empty() && pending_image_upload.empty();
        }
        for (const std::weak_ptr<resource::Mesh>& weak : meshes)
        {
            if (std::shared_ptr<resource::Mesh> mesh = weak.lock())
            {
                if (CreateRenderData(device, *mesh))
                {
                    completed_component_mask |= ecs::geometry_component_mask;
                }
            }
        }
        for (const std::weak_ptr<resource::Font>& weak : fonts)
        {
            if (std::shared_ptr<resource::Font> font = weak.lock())
            {
                CreateRenderData(device, *font);
            }
        }
        for (const PendingImageUpload& pending : images)
        {
            if (std::shared_ptr<resource::Image> image = pending.image.lock())
            {
                if (CreateRenderData(device, *image, pending.format, true))
                {
                    completed_component_mask |= ecs::material_component_mask;
                }
            }
        }

        if (out_completed_component_mask)
        {
            *out_completed_component_mask = completed_component_mask;
        }
        return queue_empty;
    }

    bool HasPendingResourceUploads()
    {
        std::lock_guard<std::mutex> lock(pending_resource_upload_mutex);
        return !pending_mesh_upload.empty() || !pending_font_upload.empty() || !pending_image_upload.empty();
    }

    bool CompressTextureBC(RHIDevice& device, Renderer& renderer, const resource::Image& image, RHIFormat format, Vector<uint8>& out_blocks, uint32& out_mip_levels)
    {
        out_blocks.clear();
        out_mip_levels = 0;
        if (!image.IsValid() || image.channels != 4 || image.format != RHIFormat::Unknown)
        {
            return false;
        }

        uint32 pipeline_index = 0;
        resource::ShaderId shader_id = resource::ShaderId::CSTextureBC1Compress;
        bool is_srgb = false;
        uint32 bytes_per_block = 8u;
        switch (format)
        {
        case RHIFormat::BC1Unorm:
            pipeline_index = 0;
            shader_id = resource::ShaderId::CSTextureBC1Compress;
            bytes_per_block = 8u;
            break;
        case RHIFormat::BC1UnormSrgb:
            pipeline_index = 0;
            shader_id = resource::ShaderId::CSTextureBC1Compress;
            is_srgb = true;
            bytes_per_block = 8u;
            break;
        case RHIFormat::BC3Unorm:
            pipeline_index = 1;
            shader_id = resource::ShaderId::CSTextureBC3Compress;
            bytes_per_block = 16u;
            break;
        case RHIFormat::BC3UnormSrgb:
            pipeline_index = 1;
            shader_id = resource::ShaderId::CSTextureBC3Compress;
            is_srgb = true;
            bytes_per_block = 16u;
            break;
        case RHIFormat::BC4Unorm:
            pipeline_index = 2;
            shader_id = resource::ShaderId::CSTextureBC4Compress;
            bytes_per_block = 8u;
            break;
        case RHIFormat::BC5Unorm:
            pipeline_index = 3;
            shader_id = resource::ShaderId::CSTextureBC5Compress;
            bytes_per_block = 16u;
            break;
        default:
            return false;
        }

        const uint32 width = static_cast<uint32>(image.width);
        const uint32 height = static_cast<uint32>(image.height);
        Vector<uint32> mip_widths;
        Vector<uint32> mip_heights;
        Vector<Size> mip_offsets;
        uint32 mip_width = width;
        uint32 mip_height = height;
        Size output_size = 0;
        while (true)
        {
            const uint32 block_count_x = (mip_width + 3u) / 4u;
            const uint32 block_count_y = (mip_height + 3u) / 4u;
            const Size mip_size = static_cast<Size>(block_count_x) * static_cast<Size>(block_count_y) * static_cast<Size>(bytes_per_block);
            mip_widths.push_back(mip_width);
            mip_heights.push_back(mip_height);
            mip_offsets.push_back(output_size);
            output_size += mip_size;
            if (mip_width == 1 && mip_height == 1)
            {
                break;
            }
            mip_width = (std::max)(1u, mip_width / 2u);
            mip_height = (std::max)(1u, mip_height / 2u);
        }
        const uint32 mip_levels = static_cast<uint32>(mip_widths.size());
        if (output_size == 0)
        {
            return false;
        }

        std::unique_ptr<RHIPipeline>& pipeline = texture_bc_compress_pipelines[pipeline_index];
        if (!pipeline)
        {
            RHIShader* shader = renderer.GetShader(shader_id);
            if (!shader)
            {
                backlog::Post("Failed to get texture BC compression shader", backlog::LogLevel::Error);
                return false;
            }

            RHIComputePipelineDesc pipeline_desc = {};
            pipeline_desc.compute_shader = shader;
            pipeline = device.CreateComputePipeline(pipeline_desc);
            if (!pipeline)
            {
                backlog::Post("Failed to create texture BC compression pipeline", backlog::LogLevel::Error);
                return false;
            }
            pipeline->SetName("TextureBCCompressPipeline");
        }

        RHITextureDesc source_desc = {};
        source_desc.width = width;
        source_desc.height = height;
        source_desc.depth = 1;
        source_desc.mip_levels = mip_levels;
        source_desc.array_layers = 1;
        source_desc.sample_count = 1;
        source_desc.format = is_srgb ? RHIFormat::R8G8B8A8UnormSrgb : RHIFormat::R8G8B8A8Unorm;
        source_desc.usage = RHIResourceUsage::Default;
        source_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::UnorderedAccess;
        std::unique_ptr<RHIResource> source_texture = device.CreateTexture(source_desc, image.pixels.data(), image.pixels.size());
        if (!source_texture)
        {
            return false;
        }
        source_texture->SetName("BC Compress Source Texture");

        RHIBufferDesc output_desc = {};
        output_desc.size = output_size;
        output_desc.usage = RHIResourceUsage::Default;
        output_desc.bind_flags = RHIBindFlags::UnorderedAccess;
        std::unique_ptr<RHIResource> output_buffer = device.CreateBuffer(output_desc);
        if (!output_buffer)
        {
            return false;
        }
        output_buffer->SetName("BC Compress Output Buffer");

        RHISubresourceHandle output_uav = {};
        RHISubresourceDesc output_uav_desc = {};
        output_uav_desc.type = RHISubresourceType::UnorderedAccess;
        output_uav_desc.buffer_offset = 0;
        output_uav_desc.buffer_size = output_size;
        output_uav_desc.buffer_stride = sizeof(uint32);
        if (!device.CreateSubresource(*output_buffer, output_uav_desc, &output_uav))
        {
            return false;
        }

        RHIBufferDesc readback_desc = {};
        readback_desc.size = output_size;
        readback_desc.usage = RHIResourceUsage::Readback;
        readback_desc.bind_flags = RHIBindFlags::None;
        std::unique_ptr<RHIResource> readback_buffer = device.CreateBuffer(readback_desc);
        if (!readback_buffer || !readback_buffer->GetMappedData())
        {
            return false;
        }
        readback_buffer->SetName("BC Compress Readback Buffer");

        RHIQueueType queue_type = RHIQueueType::Graphics;
        RHIContext* context = device.GetContext(queue_type);
        std::unique_ptr<RHICommandAllocator> command_allocator = device.CreateCommandAllocator(queue_type);
        std::unique_ptr<RHICommandList> command_list = device.CreateCommandList(queue_type);
        if (!context || !command_allocator || !command_list)
        {
            return false;
        }

        command_allocator->Reset();
        command_list->Begin(*command_allocator);
        Vector<RHISubresourceHandle> mip_srvs;
        if (!GenerateTextureMips(device, renderer, *command_list, *source_texture, &mip_srvs))
        {
            command_list->End();
            return false;
        }
        if (mip_srvs.empty())
        {
            mip_srvs.resize(1);
            RHISubresourceDesc source_srv_desc = {};
            source_srv_desc.type = RHISubresourceType::ShaderResource;
            source_srv_desc.format = source_desc.format;
            source_srv_desc.first_slice = 0;
            source_srv_desc.slice_count = 1;
            source_srv_desc.first_mip = 0;
            source_srv_desc.mip_count = 1;
            if (!device.CreateSubresource(*source_texture, source_srv_desc, &mip_srvs[0]))
            {
                command_list->End();
                return false;
            }
            command_list->TransitionResource(*source_texture, RHIResourceState::Undefined, RHIResourceState::ShaderRead);
        }
        command_list->TransitionResource(*output_buffer, RHIResourceState::Undefined, RHIResourceState::ShaderWrite);
        command_list->SetComputePipeline(*pipeline);

        for (uint32 mip_index = 0; mip_index < mip_levels; ++mip_index)
        {
            const uint32 block_count_x = (mip_widths[mip_index] + 3u) / 4u;
            const uint32 block_count_y = (mip_heights[mip_index] + 3u) / 4u;
            TextureBCCompressPushConstants push_constants = {};
            push_constants.source_srv = static_cast<uint>(mip_srvs[mip_index].descriptor_index);
            push_constants.output_uav = static_cast<uint>(output_uav.descriptor_index);
            push_constants.output_offset = static_cast<uint>(mip_offsets[mip_index] / sizeof(uint32));
            if (is_srgb)
            {
                push_constants.flags |= TEXTURE_BC_COMPRESS_FLAGS_IS_SRGB;
            }
            command_list->PushConstants(RHIShaderStage::Compute, &push_constants, sizeof(push_constants), 0);
            command_list->Dispatch((block_count_x + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D, (block_count_y + DISPATCH_THREAD_GROUP_2D - 1) / DISPATCH_THREAD_GROUP_2D, 1u);
        }
        command_list->UAVBarrier(*output_buffer);
        command_list->TransitionResource(*output_buffer, RHIResourceState::ShaderWrite, RHIResourceState::CopySource);
        command_list->CopyBuffer(*readback_buffer, 0, *output_buffer, 0, output_size);
        command_list->End();

        std::unique_ptr<RHIFence> fence = device.CreateFence(0);
        if (fence)
        {
            const uint64 fence_value = context->Submit(*command_list, fence.get());
            if (fence_value > 0)
            {
                fence->Wait(fence_value);
            }
            else
            {
                context->WaitIdle();
            }
        }
        else
        {
            context->Submit(*command_list);
            context->WaitIdle();
        }

        out_blocks.resize(output_size);
        std::memcpy(out_blocks.data(), readback_buffer->GetMappedData(), output_size);
        out_mip_levels = mip_levels;
        return true;
    }

    bool BakeImpostor(RHIDevice& device, Renderer& renderer, resource::Mesh& mesh,
        const resource::Material& material, uint32 grid_size, uint32 tile_resolution,
        impostor::ImpostorLayout layout)
    {
        if (grid_size == 0 || tile_resolution == 0 || mesh.submeshes.empty() || material.slots.empty() || !mesh.render_data.IsValid())
        {
            return false;
        }

        Vector<ShaderTransform> shader_transforms(1);
        shader_transforms[0].Init();
        shader_transforms[0].world_transform = math::IDENTITY_MATRIX;
        shader_transforms[0].normal_transform_row0 = float3(1.0f, 0.0f, 0.0f);
        shader_transforms[0].normal_transform_row1 = float3(0.0f, 1.0f, 0.0f);
        shader_transforms[0].normal_transform_row2 = float3(0.0f, 0.0f, 1.0f);

        Vector<ShaderGeometry> shader_geometries(mesh.submeshes.size());
        for (Size i = 0; i < mesh.submeshes.size(); ++i)
        {
            WriteShaderGeometry(mesh, i, shader_geometries[i]);
        }

        Vector<ShaderMaterial> shader_materials(material.slots.size());
        for (Size i = 0; i < material.slots.size(); ++i)
        {
            WriteShaderMaterial(material.slots[i], shader_materials[i]);
        }
        const uint32 transform_index_data = 0;

        RHISubresourceHandle transform_srv = {};
        RHISubresourceHandle geometry_srv = {};
        RHISubresourceHandle material_srv = {};
        RHISubresourceHandle transform_index_srv = {};
        std::unique_ptr<RHIResource> transform_buffer = CreateStructuredBuffer(device, "Impostor Transform Buffer", shader_transforms.data(), shader_transforms.size() * sizeof(ShaderTransform), sizeof(ShaderTransform), RHIBindFlags::ShaderResource, &transform_srv, nullptr);
        std::unique_ptr<RHIResource> geometry_buffer = CreateStructuredBuffer(device, "Impostor Geometry Buffer", shader_geometries.data(), shader_geometries.size() * sizeof(ShaderGeometry), sizeof(ShaderGeometry), RHIBindFlags::ShaderResource, &geometry_srv, nullptr);
        std::unique_ptr<RHIResource> material_buffer = CreateStructuredBuffer(device, "Impostor Material Buffer", shader_materials.data(), shader_materials.size() * sizeof(ShaderMaterial), sizeof(ShaderMaterial), RHIBindFlags::ShaderResource, &material_srv, nullptr);
        std::unique_ptr<RHIResource> transform_index_buffer = CreateStructuredBuffer(device, "Impostor Transform Index Buffer", &transform_index_data, sizeof(uint32), sizeof(uint32), RHIBindFlags::ShaderResource, &transform_index_srv, nullptr);
        if (!transform_buffer || !geometry_buffer || !material_buffer || !transform_index_buffer)
        {
            return false;
        }

        ShaderFrame shader_frame;
        shader_frame.Init();
        shader_frame.scene.transform_buffer = transform_srv.descriptor_index;
        shader_frame.scene.geometrybuffer = geometry_srv.descriptor_index;
        shader_frame.scene.materialbuffer = material_srv.descriptor_index;
        RHISubresourceHandle frame_cbv = {};
        std::unique_ptr<RHIResource> frame_buffer = CreateConstantBuffer(device, &shader_frame, sizeof(ShaderFrame), frame_cbv);
        if (!frame_buffer)
        {
            return false;
        }

        math::AABB mesh_bounds;
        mesh_bounds.Invalidate();
        for (const resource::Submesh& submesh : mesh.submeshes)
        {
            mesh_bounds.Merge(submesh.local_bounds);
        }
        const float3 bounds_min = mesh_bounds.min;
        const float3 bounds_max = mesh_bounds.max;
        const float3 center = float3((bounds_min.x + bounds_max.x) * 0.5f, (bounds_min.y + bounds_max.y) * 0.5f, (bounds_min.z + bounds_max.z) * 0.5f);
        const float radius = 0.5f * math::Length(float3(bounds_max.x - bounds_min.x, bounds_max.y - bounds_min.y, bounds_max.z - bounds_min.z));
        if (radius <= 0.0f)
        {
            return false;
        }
        const float capture_distance = radius * 2.0f;
        const float near_plane = capture_distance - radius;
        const float far_plane = capture_distance + radius;

        Vector<std::unique_ptr<RHIResource>> view_buffers;
        Vector<RHISubresourceHandle> view_cbvs(static_cast<Size>(grid_size) * grid_size);
        view_buffers.reserve(static_cast<Size>(grid_size) * grid_size);
        for (uint32 y = 0; y < grid_size; ++y)
        {
            for (uint32 x = 0; x < grid_size; ++x)
            {
                const float3 oct_direction = impostor::CellCaptureDirection(x, y, grid_size, layout);
                const float3 direction = float3(oct_direction.x, oct_direction.z, oct_direction.y);
                const float3 eye = float3(center.x + direction.x * capture_distance, center.y + direction.y * capture_distance, center.z + direction.z * capture_distance);
                const DirectX::XMVECTOR eye_vector = DirectX::XMVectorSet(eye.x, eye.y, eye.z, 1.0f);
                const DirectX::XMVECTOR forward_vector = DirectX::XMVectorSet(-direction.x, -direction.y, -direction.z, 0.0f);
                const bool near_vertical = std::abs(direction.y) > 0.99f;
                const DirectX::XMVECTOR up_vector = near_vertical ? DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f) : DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                const DirectX::XMMATRIX view_matrix = DirectX::XMMatrixLookToLH(eye_vector, forward_vector, up_vector);
                const DirectX::XMMATRIX projection_matrix = DirectX::XMMatrixOrthographicLH(radius * 2.0f, radius * 2.0f, far_plane, near_plane);
                const DirectX::XMMATRIX view_projection_matrix = DirectX::XMMatrixMultiply(view_matrix, projection_matrix);

                ShaderView shader_view;
                shader_view.Init();
                ShaderCamera& camera = shader_view.camera;
                camera.position = eye;
                camera.forward = float3(-direction.x, -direction.y, -direction.z);
                camera.up = near_vertical ? float3(0.0f, 0.0f, 1.0f) : float3(0.0f, 1.0f, 0.0f);
                camera.z_near = near_plane;
                camera.z_far = far_plane;
                camera.internal_resolution = { tile_resolution, tile_resolution };
                camera.exposure = 1.0f;
                DirectX::XMStoreFloat4x4(&camera.view, view_matrix);
                DirectX::XMStoreFloat4x4(&camera.projection, projection_matrix);
                DirectX::XMStoreFloat4x4(&camera.view_projection, view_projection_matrix);
                DirectX::XMStoreFloat4x4(&camera.inv_view_projection, DirectX::XMMatrixInverse(nullptr, view_projection_matrix));
                shader_view.transform_index_buffer = transform_index_srv.descriptor_index;

                RHISubresourceHandle& cell_cbv = view_cbvs[static_cast<Size>(y) * grid_size + x];
                std::unique_ptr<RHIResource> view_buffer = CreateConstantBuffer(device, &shader_view, sizeof(ShaderView), cell_cbv);
                if (!view_buffer)
                {
                    return false;
                }
                view_buffers.push_back(std::move(view_buffer));
            }
        }

        const uint32 atlas_size = grid_size * tile_resolution;
        RHISubresourceHandle albedo_rtv = {};
        RHISubresourceHandle normal_rtv = {};
        RHISubresourceHandle depth_rtv = {};
        RHISubresourceHandle depth_stencil_dsv = {};
        std::unique_ptr<RHIResource> albedo_atlas = CreateRenderTargetTexture(device, atlas_size, atlas_size, RHIFormat::R8G8B8A8UnormSrgb, RHIBindFlags::RenderTarget, RHISubresourceType::RenderTarget, albedo_rtv);
        std::unique_ptr<RHIResource> normal_atlas = CreateRenderTargetTexture(device, atlas_size, atlas_size, RHIFormat::R8G8B8A8Unorm, RHIBindFlags::RenderTarget, RHISubresourceType::RenderTarget, normal_rtv);
        std::unique_ptr<RHIResource> depth_atlas = CreateRenderTargetTexture(device, atlas_size, atlas_size, RHIFormat::R16Float, RHIBindFlags::RenderTarget, RHISubresourceType::RenderTarget, depth_rtv);
        std::unique_ptr<RHIResource> depth_stencil = CreateRenderTargetTexture(device, atlas_size, atlas_size, RHIFormat::D32Float, RHIBindFlags::DepthStencil, RHISubresourceType::DepthStencil, depth_stencil_dsv);
        if (!albedo_atlas || !normal_atlas || !depth_atlas || !depth_stencil)
        {
            return false;
        }

        RHIShader* vertex_shader = renderer.GetShader(resource::ShaderId::VSObjectCommon);
        std::unique_ptr<RHIPipeline> capture_pipelines[2][2] = {};
        for (uint32 masked = 0; masked < 2; ++masked)
        {
            RHIShader* pixel_shader = renderer.GetShader(masked == 1 ? resource::ShaderId::PSObjectImpostorCaptureMasked : resource::ShaderId::PSObjectImpostorCapture);
            for (uint32 cull_none = 0; cull_none < 2; ++cull_none)
            {
                RHIGraphicsPipelineDesc pipeline_desc = {};
                pipeline_desc.vertex_shader = vertex_shader;
                pipeline_desc.pixel_shader = pixel_shader;
                pipeline_desc.sample_count = 1;
                pipeline_desc.depth_stencil_format = RHIFormat::D32Float;
                pipeline_desc.depth_stencil.depth_test = true;
                pipeline_desc.depth_stencil.depth_write = true;
                pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
                pipeline_desc.blend.enable = false;
                pipeline_desc.raster.cull_mode = cull_none == 1 ? RHICullMode::None : RHICullMode::Back;
                pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
                pipeline_desc.render_target_formats = { RHIFormat::R8G8B8A8UnormSrgb, RHIFormat::R8G8B8A8Unorm, RHIFormat::R16Float };
                capture_pipelines[masked][cull_none] = device.CreateGraphicsPipeline(pipeline_desc);
                if (!capture_pipelines[masked][cull_none])
                {
                    return false;
                }
            }
        }

        RHIContext* context = device.GetContext(RHIQueueType::Graphics);
        std::unique_ptr<RHICommandAllocator> command_allocator = device.CreateCommandAllocator(RHIQueueType::Graphics);
        std::unique_ptr<RHICommandList> command_list = device.CreateCommandList(RHIQueueType::Graphics);
        if (!context || !command_allocator || !command_list)
        {
            return false;
        }

        command_allocator->Reset();
        command_list->Begin(*command_allocator);
        command_list->TransitionResource(*albedo_atlas, RHIResourceState::Undefined, RHIResourceState::RenderTarget);
        command_list->TransitionResource(*normal_atlas, RHIResourceState::Undefined, RHIResourceState::RenderTarget);
        command_list->TransitionResource(*depth_atlas, RHIResourceState::Undefined, RHIResourceState::RenderTarget);
        command_list->TransitionResource(*depth_stencil, RHIResourceState::Undefined, RHIResourceState::DepthWrite);

        RHISubresourceBinding albedo_binding = { albedo_atlas.get(), albedo_rtv };
        RHISubresourceBinding normal_binding = { normal_atlas.get(), normal_rtv };
        RHISubresourceBinding depth_binding = { depth_atlas.get(), depth_rtv };
        RHISubresourceBinding depth_stencil_binding = { depth_stencil.get(), depth_stencil_dsv };
        const Vector<RHISubresourceBinding> color_targets = { albedo_binding, normal_binding, depth_binding };
        command_list->SetRenderTargets(color_targets, &depth_stencil_binding);
        const RHIClearColor clear_color = { 0.0f, 0.0f, 0.0f, 0.0f };
        command_list->ClearRenderTarget(albedo_binding, clear_color);
        command_list->ClearRenderTarget(normal_binding, clear_color);
        command_list->ClearRenderTarget(depth_binding, clear_color);
        command_list->ClearDepthStencil(depth_stencil_binding, OPTIMIZED_FAST_CLEAR_DEPTH, 0);

        RHISubresourceBinding frame_binding = { frame_buffer.get(), frame_cbv };
        command_list->SetIndexBuffer(*mesh.render_data.buffer, sizeof(uint32), mesh.render_data.indices.offset, mesh.render_data.indices.size);

        for (uint32 y = 0; y < grid_size; ++y)
        {
            for (uint32 x = 0; x < grid_size; ++x)
            {
                RHIViewport viewport = {};
                viewport.x = static_cast<float>(x * tile_resolution);
                viewport.y = static_cast<float>(y * tile_resolution);
                viewport.width = static_cast<float>(tile_resolution);
                viewport.height = static_cast<float>(tile_resolution);
                viewport.min_depth = 0.0f;
                viewport.max_depth = 1.0f;
                RHIRect scissor = {};
                scissor.x = x * tile_resolution;
                scissor.y = y * tile_resolution;
                scissor.width = tile_resolution;
                scissor.height = tile_resolution;
                command_list->SetViewport(viewport);
                command_list->SetScissor(scissor);

                RHISubresourceBinding view_binding = { view_buffers[static_cast<Size>(y) * grid_size + x].get(), view_cbvs[static_cast<Size>(y) * grid_size + x] };
                for (Size submesh_index = 0; submesh_index < mesh.submeshes.size(); ++submesh_index)
                {
                    const resource::Submesh& submesh = mesh.submeshes[submesh_index];
                    if (submesh.material_slot >= material.slots.size())
                    {
                        continue;
                    }
                    const resource::MaterialSlot& material_slot = material.slots[submesh.material_slot];
                    const uint32 masked = material_slot.settings.blend_mode == resource::MaterialBlendMode::Masked ? 1u : 0u;
                    const uint32 cull_none = material_slot.settings.double_sided ? 1u : 0u;
                    command_list->SetGraphicsPipeline(*capture_pipelines[masked][cull_none]);
                    command_list->SetConstantBuffer(RHIShaderStage::Vertex, CBSLOT_RENDERER_FRAME, frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Vertex, CBSLOT_RENDERER_CAMERA, view_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Pixel, CBSLOT_RENDERER_FRAME, frame_binding);
                    command_list->SetConstantBuffer(RHIShaderStage::Pixel, CBSLOT_RENDERER_CAMERA, view_binding);
                    command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);

                    ObjectPushConstants push;
                    push.Init();
                    push.instance_offset = 0;
                    push.geometry_index = static_cast<uint32>(submesh_index);
                    push.material_index = submesh.material_slot;
                    command_list->PushConstants(RHIShaderStage::Vertex, &push, sizeof(ObjectPushConstants), 0);
                    command_list->DrawIndexed(submesh.index_count, 1, submesh.first_index, 0, 0);
                }
            }
        }

        command_list->End();
        std::unique_ptr<RHIFence> fence = device.CreateFence(0);
        const uint64 fence_value = context->Submit(*command_list, fence.get());
        if (fence_value > 0)
        {
            fence->Wait(fence_value);
        }
        else
        {
            context->WaitIdle();
        }

        std::shared_ptr<resource::Image> albedo_image = ReadbackTextureToImage(device, *albedo_atlas, RHIResourceState::RenderTarget, atlas_size, 4, RHIFormat::R8G8B8A8UnormSrgb, 4);
        std::shared_ptr<resource::Image> normal_image = ReadbackTextureToImage(device, *normal_atlas, RHIResourceState::RenderTarget, atlas_size, 4, RHIFormat::R8G8B8A8Unorm, 4);
        std::shared_ptr<resource::Image> depth_image = ReadbackTextureToImage(device, *depth_atlas, RHIResourceState::RenderTarget, atlas_size, 2, RHIFormat::R16Float, 1);
        if (!albedo_image || !normal_image || !depth_image)
        {
            return false;
        }

        mesh.impostor.grid_size = grid_size;
        mesh.impostor.radius = radius;
        mesh.impostor.center = center;
        mesh.impostor.albedo = albedo_image;
        mesh.impostor.normal = normal_image;
        mesh.impostor.depth = depth_image;
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

        const Size stream_slots = mesh.dynamic_vertex_streams ? static_cast<Size>(max_frames_in_flight) : 1;
        const Size positions_size = mesh.positions.size() * sizeof(float3) * stream_slots;
        const Size colors_size = mesh.colors.size() * sizeof(float4);
        const Size normals_size = mesh.normals.size() * sizeof(float3) * stream_slots;
        const Size tangents_size = mesh.tangents.size() * sizeof(float4);
        const Size texcoords_size = mesh.texcoords.size() * sizeof(float2);
        const bool has_skinning_stream = mesh.bone_indices.size() == mesh.positions.size() && mesh.bone_weights.size() == mesh.positions.size();
        const Size bone_indices_size = has_skinning_stream ? mesh.bone_indices.size() * sizeof(uint4) : 0;
        const Size bone_weights_size = has_skinning_stream ? mesh.bone_weights.size() * sizeof(float4) : 0;
        const Size indices_size = mesh.indices.size() * sizeof(uint32);

        Vector<uint2> adjacency_ranges; // per vertex (start_index, count)
        Vector<uint32> adjacency_triangles; // flattened triangle indices
        if (mesh.dynamic_vertex_streams)
        {
            const Size triangle_count = mesh.indices.size() / 3;
            adjacency_ranges.assign(mesh.positions.size(), uint2(0, 0));
            for (Size triangle = 0; triangle < triangle_count; ++triangle)
            {
                for (Size corner = 0; corner < 3; ++corner)
                {
                    const uint32 vertex = mesh.indices[triangle * 3 + corner];
                    if (vertex < adjacency_ranges.size())
                    {
                        ++adjacency_ranges[vertex].y; // count triangles
                    }
                }
            }
            uint32 running = 0;
            // prefix sum
            for (uint2& range : adjacency_ranges)
            {
                range.x = running;
                running += range.y;
                range.y = 0; // will reuse this
            }
            adjacency_triangles.resize(running);
            for (Size triangle = 0; triangle < triangle_count; ++triangle)
            {
                for (Size corner = 0; corner < 3; ++corner)
                {
                    const uint32 vertex = mesh.indices[triangle * 3 + corner];
                    if (vertex < adjacency_ranges.size())
                    {
                        adjacency_triangles[adjacency_ranges[vertex].x + adjacency_ranges[vertex].y] = static_cast<uint32>(triangle);
                        ++adjacency_ranges[vertex].y;
                    }
                }
            }
        }
        const Size adjacency_ranges_size = adjacency_ranges.size() * sizeof(uint2);
        const Size adjacency_triangles_size = adjacency_triangles.size() * sizeof(uint32);
        Size total_size = 0;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(float3))) + positions_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(float4))) + colors_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(float3))) + normals_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(float4))) + tangents_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(float2))) + texcoords_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(uint4))) + bone_indices_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(float4))) + bone_weights_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(uint32))) + indices_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(uint2))) + adjacency_ranges_size;
        total_size = math::Align(total_size, static_cast<Size>(sizeof(uint32))) + adjacency_triangles_size;
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
        Size bone_indices_offset = 0;
        Size bone_weights_offset = 0;
        Size indices_offset = 0;
        Size adjacency_ranges_offset = 0;
        Size adjacency_triangles_offset = 0;

        PackBufferSubresource(mesh.positions, packed_data, positions_offset, positions_size, sizeof(float3), offset, stream_slots);
        PackBufferSubresource(mesh.colors, packed_data, colors_offset, colors_size, sizeof(float4), offset);
        PackBufferSubresource(mesh.normals, packed_data, normals_offset, normals_size, sizeof(float3), offset, stream_slots);
        PackBufferSubresource(mesh.tangents, packed_data, tangents_offset, tangents_size, sizeof(float4), offset);
        PackBufferSubresource(mesh.texcoords, packed_data, texcoords_offset, texcoords_size, sizeof(float2), offset);
        PackBufferSubresource(mesh.bone_indices, packed_data, bone_indices_offset, bone_indices_size, sizeof(uint4), offset);
        PackBufferSubresource(mesh.bone_weights, packed_data, bone_weights_offset, bone_weights_size, sizeof(float4), offset);
        PackBufferSubresource(mesh.indices, packed_data, indices_offset, indices_size, sizeof(uint32), offset);
        PackBufferSubresource(adjacency_ranges, packed_data, adjacency_ranges_offset, adjacency_ranges_size, sizeof(uint2), offset);
        PackBufferSubresource(adjacency_triangles, packed_data, adjacency_triangles_offset, adjacency_triangles_size, sizeof(uint32), offset);

        RHIBufferDesc buffer_desc = {};
        buffer_desc.size = total_size;
        buffer_desc.usage = RHIResourceUsage::Default;
        buffer_desc.bind_flags = RHIBindFlags::VertexBuffer | RHIBindFlags::IndexBuffer | RHIBindFlags::ShaderResource;
        if (mesh.dynamic_vertex_streams)
        {
            buffer_desc.bind_flags = buffer_desc.bind_flags | RHIBindFlags::UnorderedAccess;
        }
        new_render_data.buffer = device.CreateBuffer(buffer_desc, packed_data.data(), packed_data.size());
        if (!new_render_data.buffer)
        {
            return false;
        }
        new_render_data.buffer->SetName(mesh.name.empty() ? String("Mesh Vertex Index Buffer") : "Mesh Vertex Index Buffer (" + mesh.name + ")");

        auto create_subresource = [&](Size buffer_offset, Size buffer_size, Size buffer_stride, bool with_uav, resource::Mesh::VBSubresource& out_subresource) -> bool
        {
            if (buffer_size == 0)
            {
                return true;
            }

            out_subresource.offset = static_cast<uint32>(buffer_offset);
            out_subresource.size = static_cast<uint32>(buffer_size);

            if (!CreateBufferView(device, *new_render_data.buffer, RHISubresourceType::ShaderResource, buffer_offset, buffer_size, buffer_stride, out_subresource.srv))
            {
                return false;
            }

            if (!with_uav)
            {
                return true;
            }

            return CreateBufferView(device, *new_render_data.buffer, RHISubresourceType::UnorderedAccess, buffer_offset, buffer_size, buffer_stride, out_subresource.uav);
        };

        if (!create_subresource(positions_offset, positions_size, sizeof(float3), false, new_render_data.positions))
        {
            return false;
        }
        if (!create_subresource(colors_offset, colors_size, sizeof(float4), false, new_render_data.colors))
        {
            return false;
        }
        if (!create_subresource(normals_offset, normals_size, sizeof(float3), mesh.dynamic_vertex_streams, new_render_data.normals))
        {
            return false;
        }
        if (!create_subresource(tangents_offset, tangents_size, sizeof(float4), false, new_render_data.tangents))
        {
            return false;
        }
        if (!create_subresource(texcoords_offset, texcoords_size, sizeof(float2), false, new_render_data.texcoords))
        {
            return false;
        }
        if (!create_subresource(bone_indices_offset, bone_indices_size, sizeof(uint4), false, new_render_data.bone_indices))
        {
            return false;
        }
        if (!create_subresource(adjacency_ranges_offset, adjacency_ranges_size, sizeof(uint2), false, new_render_data.adjacency_ranges))
        {
            return false;
        }
        if (!create_subresource(adjacency_triangles_offset, adjacency_triangles_size, sizeof(uint32), false, new_render_data.adjacency_triangles))
        {
            return false;
        }
        if (!create_subresource(bone_weights_offset, bone_weights_size, sizeof(float4), false, new_render_data.bone_weights))
        {
            return false;
        }
        if (!create_subresource(indices_offset, indices_size, sizeof(uint32), false, new_render_data.indices))
        {
            return false;
        }

        mesh.render_data = std::move(new_render_data);
        return true;
    }

    bool CreateRenderData(RHIDevice& device, resource::Image& image, RHIFormat format, bool generate_mips)
    {
        const bool compressed_texture = image.format != RHIFormat::Unknown;
        if (!image.IsValid() || (!compressed_texture && image.channels != 4))
        {
            return false;
        }

        uint32 mip_levels = compressed_texture ? image.mip_levels : 1;
        if (mip_levels == 0)
        {
            mip_levels = 1;
        }
        if (generate_mips && !compressed_texture)
        {
            uint32 mip_width = static_cast<uint32>(image.width);
            uint32 mip_height = static_cast<uint32>(image.height);
            while (mip_width > 1 || mip_height > 1)
            {
                mip_width = (std::max)(1u, mip_width / 2u);
                mip_height = (std::max)(1u, mip_height / 2u);
                ++mip_levels;
            }
        }

        const RHIFormat texture_format = compressed_texture ? image.format : format;
        if (image.render_data.IsValid() && image.render_data.format == texture_format && image.render_data.mip_levels == mip_levels)
        {
            return true;
        }

        RHITextureDesc texture_desc = {};
        texture_desc.width = static_cast<uint32>(image.width);
        texture_desc.height = static_cast<uint32>(image.height);
        texture_desc.depth = 1;
        texture_desc.mip_levels = mip_levels;
        texture_desc.array_layers = image.is_cube ? 6u : 1u;
        texture_desc.is_cube = image.is_cube;
        texture_desc.sample_count = 1;
        texture_desc.format = texture_format;
        texture_desc.usage = RHIResourceUsage::Default;
        texture_desc.bind_flags = RHIBindFlags::ShaderResource;
        if (generate_mips && !compressed_texture)
        {
            texture_desc.bind_flags = texture_desc.bind_flags | RHIBindFlags::UnorderedAccess;
        }
        image.ClearRenderData();

        resource::Image::RenderData new_render_data = {};
        new_render_data.texture = device.CreateTexture(texture_desc, image.pixels.data(), image.pixels.size());
        if (!new_render_data.texture)
        {
            return false;
        }
        new_render_data.texture->SetName(image.name.empty() ? String("Image Texture") : "Image Texture (" + image.name + ")");

        RHISubresourceDesc texture_srv_desc = {};
        texture_srv_desc.type = RHISubresourceType::ShaderResource;
        texture_srv_desc.first_slice = 0;
        texture_srv_desc.slice_count = image.is_cube ? 6u : 1u;
        texture_srv_desc.first_mip = 0;
        texture_srv_desc.mip_count = texture_desc.mip_levels;
        if (!device.CreateSubresource(*new_render_data.texture, texture_srv_desc, &new_render_data.srv))
        {
            return false;
        }

        if (generate_mips && !compressed_texture)
        {
            EnqueueTextureMipGeneration(new_render_data.texture);
        }

        new_render_data.format = texture_format;
        new_render_data.mip_levels = texture_desc.mip_levels;
        image.render_data = std::move(new_render_data);
        return true;
    }

    bool CreateRenderData(RHIDevice& device, resource::Font& font)
    {
        if (!font.IsValid() || !font.atlas.IsValid())
        {
            return false;
        }

        if (font.render_data.IsValid() && !font.atlas.dirty && font.render_data.atlas_width == font.atlas.width && font.render_data.atlas_height == font.atlas.height)
        {
            return true;
        }

        RHITextureDesc texture_desc = {};
        texture_desc.width = static_cast<uint32>(font.atlas.width);
        texture_desc.height = static_cast<uint32>(font.atlas.height);
        texture_desc.depth = 1;
        texture_desc.mip_levels = 1;
        texture_desc.array_layers = 1;
        texture_desc.sample_count = 1;
        texture_desc.format = RHIFormat::R8Unorm;
        texture_desc.usage = RHIResourceUsage::Default;
        texture_desc.bind_flags = RHIBindFlags::ShaderResource;
        font.ClearRenderData();

        resource::Font::RenderData new_render_data = {};
        new_render_data.atlas_texture = device.CreateTexture(texture_desc, font.atlas.pixels.data(), font.atlas.pixels.size());
        if (!new_render_data.atlas_texture)
        {
            return false;
        }
        new_render_data.atlas_texture->SetName(font.name.empty() ? String("Font Atlas Texture") : "Font Atlas Texture (" + font.name + ")");

        RHISubresourceDesc texture_srv_desc = {};
        texture_srv_desc.type = RHISubresourceType::ShaderResource;
        texture_srv_desc.first_slice = 0;
        texture_srv_desc.slice_count = 1;
        texture_srv_desc.first_mip = 0;
        texture_srv_desc.mip_count = 1;
        if (!device.CreateSubresource(*new_render_data.atlas_texture, texture_srv_desc, &new_render_data.atlas_srv))
        {
            return false;
        }

        new_render_data.atlas_width = font.atlas.width;
        new_render_data.atlas_height = font.atlas.height;
        font.atlas.dirty = false;
        font.render_data = std::move(new_render_data);
        return true;
    }

}
