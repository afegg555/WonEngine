#include "FrameGraph.h"

#include "Backlog.h"
#include "FrameContext.h"
#include "MathUtils.h"
#include "RHICommandList.h"
#include "RHIDevice.h"
#include "StableHash.h"

namespace won::rendering
{
    RHIResource* FrameGraphPassContext::GetResource(FrameResourceId resource) const
    {
        return resource < frame_resource_count ? frame_resources[resource].resource : nullptr;
    }

    void FrameGraph::Initialize(RHIDevice* in_device)
    {
        device = in_device;

		// tier 2 lets every resource type share one heap
        if (device->HasFeature(RHIDeviceFeature::MixedResourceHeap))
        {
            heaps.resize(1);
            return;
        }
        else
        {
            heaps.resize(static_cast<Size>(RHIMemoryCategory::Count));
            for (Size i = 0; i < heaps.size(); ++i)
            {
                heaps[i].category = static_cast<RHIMemoryCategory>(i);
            }
        }
    }

    void FrameGraph::BeginFrame(FrameContext& in_frame_context)
    {
        frame_context = &in_frame_context;

        frame_resources.clear();
        passes.clear();
        buffer_uploads.clear();
        upload_pass_index = 0;
        has_upload_pass = false;

		// drop pooled resources that have not been used for a few frames
        for (auto pooled_iterator = pooled_resources.begin(); pooled_iterator != pooled_resources.end(); )
        {
            PooledResource& pooled = pooled_iterator->second;
            pooled.frame_resource = invalid_frame_resource;
            ++pooled.unused_frames;
			if (pooled.unused_frames <= max_frames_in_flight * 2) // heuristic: keep resources for a few frames
            {
                ++pooled_iterator;
                continue;
            }

            for (const PooledSubresource& subresource : pooled.subresources)
            {
                device->ReleaseSubresource(subresource.desc, subresource.handle);
            }
            frame_context->RemoveResourceDeferred(std::move(pooled.resource));
            pooled_iterator = pooled_resources.erase(pooled_iterator);
        }
    }

    FrameGraph::PooledResource& FrameGraph::CreatePooledResource(uint32 scope, const char* name)
    {
        uint64 key = StableHash(name);
        key ^= scope;
        key *= stable_hash_prime;

        auto found = pooled_resources.find(key);
        if (found != pooled_resources.end())
        {
            if (found->second.scope != scope || found->second.name != name)
            {
                backlog::Post((String("FrameGraph: '") + name + "' and '" + found->second.name + "' collide on the same pooled resource key").c_str(), backlog::LogLevel::Error);
            }
            return found->second;
        }

        PooledResource& created = pooled_resources[key];
        created.scope = scope;
        created.name = name;
        return created;
    }

    FrameResourceId FrameGraph::Import(RHIResource& resource, RHIResourceState state)
    {
        for (Size resource_index = 0; resource_index < frame_resources.size(); ++resource_index)
        {
            if (frame_resources[resource_index].resource == &resource)
            {
                return static_cast<FrameResourceId>(resource_index);
            }
        }

        const FrameResourceId id = static_cast<FrameResourceId>(frame_resources.size());
        FrameResource& frame_resource = frame_resources.emplace_back();
        frame_resource.resource = &resource;
        frame_resource.name = resource.GetName().c_str();
        frame_resource.desc = resource.GetDesc();
        frame_resource.state = state;
        frame_resource.entry_state = state;
        return id;
    }

    FrameResourceId FrameGraph::CreateBuffer(uint32 scope, const char* name, const RHIBufferDesc& desc)
    {
        PooledResource& pooled = CreatePooledResource(scope, name);
        if (pooled.frame_resource != invalid_frame_resource)
        {
            const FrameResource& existing = frame_resources[pooled.frame_resource];
            if (existing.desc.type != RHIResourceType::Buffer || !(existing.desc.buffer_desc == desc))
            {
                backlog::Post((String("FrameGraph: '") + name + "' is already declared this frame with a different description").c_str(), backlog::LogLevel::Warning);
            }
            return pooled.frame_resource;
        }

        const FrameResourceId id = static_cast<FrameResourceId>(frame_resources.size());
        FrameResource& frame_resource = frame_resources.emplace_back();
        frame_resource.scope = scope;
        frame_resource.name = name;
        frame_resource.desc.type = RHIResourceType::Buffer;
        frame_resource.desc.buffer_desc = desc;
        frame_resource.transient = true;
        pooled.frame_resource = id;
        return id;
    }

    FrameResourceId FrameGraph::CreateTexture(uint32 scope, const char* name, const RHITextureDesc& desc)
    {
        PooledResource& pooled = CreatePooledResource(scope, name);
        if (pooled.frame_resource != invalid_frame_resource)
        {
            const FrameResource& existing = frame_resources[pooled.frame_resource];
            if (existing.desc.type == RHIResourceType::Buffer || !(existing.desc.texture_desc == desc))
            {
                backlog::Post((String("FrameGraph: '") + name + "' is already declared this frame with a different description").c_str(), backlog::LogLevel::Warning);
            }
            return pooled.frame_resource;
        }

        const FrameResourceId id = static_cast<FrameResourceId>(frame_resources.size());
        FrameResource& frame_resource = frame_resources.emplace_back();
        frame_resource.scope = scope;
        frame_resource.name = name;
        frame_resource.desc.type = RHIResourceType::Texture2D;
        frame_resource.desc.texture_desc = desc;
        frame_resource.transient = true;
        pooled.frame_resource = id;
        return id;
    }

    RHISubresourceHandle FrameGraph::CreateSubresource(FrameResourceId resource, const RHISubresourceDesc& desc)
    {
        if (resource >= frame_resources.size())
        {
            return {};
        }

        const FrameResource& frame_resource = frame_resources[resource];

        RHISubresourceDesc pooled_desc = desc;
        if (frame_resource.desc.type == RHIResourceType::Buffer
            && desc.buffer_offset == 0
            && desc.buffer_size == frame_resource.desc.buffer_desc.size)
        {
            pooled_desc.buffer_size = 0;
        }

		PooledResource& pooled = CreatePooledResource(frame_resource.scope, frame_resource.name); // if exists, will return existing pooled resource
        for (const PooledSubresource& subresource : pooled.subresources)
        {
            if (subresource.desc == pooled_desc)
            {
                return subresource.handle;
            }
        }

        RHISubresourceHandle handle = {};
        if (!device->ReserveSubresource(pooled_desc, &handle))
        {
            return {};
        }

        PooledSubresource& subresource = pooled.subresources.emplace_back();
        subresource.desc = pooled_desc;
        subresource.handle = handle;
        return handle;
    }

    void FrameGraph::MarkNoCull(FrameResourceId resource)
    {
        if (resource < frame_resources.size())
        {
            frame_resources[resource].no_cull = true;
        }
    }

    bool FrameGraph::QueueBufferUpload(FrameResourceId destination, const void* data, Size size, Size destination_offset)
    {
        if (destination >= frame_resources.size() || frame_resources[destination].desc.type != RHIResourceType::Buffer || size == 0)
        {
            return false;
        }

        FrameUploadAllocation allocation = {};
        const Size alignment = device->GetMinOffsetAlignment(frame_resources[destination].desc.buffer_desc);
        if (!frame_context->AllocateFrameUpload(*device, size, alignment, allocation))
        {
            return false;
        }
        std::memcpy(allocation.mapped_data, data, size);

        if (!has_upload_pass)
        {
            has_upload_pass = true;
            upload_pass_index = passes.size();
            AddPass("Upload Buffers", {}, [this](const FrameGraphPassContext& pass_context)
            {
                for (const BufferUpload& upload : buffer_uploads)
                {
                    RHIResource* destination_resource = pass_context.GetResource(upload.destination);
                    if (!destination_resource)
                    {
                        continue;
                    }

                    pass_context.command_list->TransitionResource(*destination_resource, RHIResourceState::Undefined, RHIResourceState::CopyDest);
                    pass_context.command_list->CopyBuffer(*destination_resource, upload.destination_offset,
                        *upload.source, upload.source_offset, upload.size);
                    pass_context.command_list->TransitionResource(*destination_resource, RHIResourceState::CopyDest, RHIResourceState::Undefined);
                }
            });
        }
        passes[upload_pass_index].accesses.push_back({ destination, RHIResourceState::Undefined, FrameResourceAccess::Type::Write });

        BufferUpload& upload = buffer_uploads.emplace_back();
        upload.destination = destination;
        upload.destination_offset = destination_offset;
        upload.source = allocation.buffer;
        upload.source_offset = allocation.buffer_offset;
        upload.size = size;
        return true;
    }

    void FrameGraph::AddPass(const char* name, Vector<FrameResourceAccess> accesses, std::function<void(const FrameGraphPassContext&)> execute)
    {
        Pass& pass = passes.emplace_back();
        pass.name = name;
        pass.accesses = std::move(accesses);
        pass.execute = std::move(execute);
    }

    void FrameGraph::Compile()
    {
        Vector<bool> resource_needed(frame_resources.size(), false);

		// cull passes and compute resource lifetimes !! backwards
        for (uint32 pass_index = (uint32)passes.size(); pass_index > 0; --pass_index)
        {
            const uint32 index = pass_index - 1;
            Pass& pass = passes[index];

            for (const FrameResourceAccess& access : pass.accesses)
            {
                if (access.type == FrameResourceAccess::Type::Read)
                {
                    continue;
                }
                if (access.resource < resource_needed.size()
                    && (resource_needed[access.resource] || frame_resources[access.resource].no_cull))
                {
                    pass.alive = true;
                    break;
                }
            }

            if (!pass.alive)
            {
                continue;
            }

            for (const FrameResourceAccess& access : pass.accesses)
            {
                if (access.resource >= frame_resources.size())
                {
                    continue;
                }

                FrameResource& frame_resource = frame_resources[access.resource];
                if (!frame_resource.alive)
                {
                    frame_resource.alive = true;
                    frame_resource.last_pass = index;
                }
                frame_resource.first_pass = index;

                if (access.type != FrameResourceAccess::Type::Write)
                {
                    resource_needed[access.resource] = true;
                }
            }
        }

        struct Placement
        {
            PooledResource* pooled = nullptr;
            FrameResourceId frame_resource = invalid_frame_resource;
            uint32 first_pass = 0;
            uint32 last_pass = 0;
            Size heap_index = ~(Size)0;
            Size heap_offset = 0;
            Vector<Size> overlaps; // placement indices sharing this memory
        };

        Vector<Placement> placements;
        placements.reserve(frame_resources.size());
        for (Size resource_index = 0; resource_index < frame_resources.size(); ++resource_index)
        {
            FrameResource& frame_resource = frame_resources[resource_index];
            if (!frame_resource.alive || !frame_resource.transient)
            {
                continue;
            }

            PooledResource& pooled = CreatePooledResource(frame_resource.scope, frame_resource.name);
            pooled.unused_frames = 0;

            const bool desc_changed = pooled.desc.type != frame_resource.desc.type
                || (frame_resource.desc.type == RHIResourceType::Buffer
                    ? !(pooled.desc.buffer_desc == frame_resource.desc.buffer_desc)
                    : !(pooled.desc.texture_desc == frame_resource.desc.texture_desc));

            if (desc_changed)
            {
                frame_context->RemoveResourceDeferred(std::move(pooled.resource));
                pooled.desc = frame_resource.desc;
                pooled.category = pooled.desc.type == RHIResourceType::Buffer
                    ? RHIMemoryCategory::Buffer
                    : ((HasBindFlag(pooled.desc.texture_desc.bind_flags, RHIBindFlags::RenderTarget) || HasBindFlag(pooled.desc.texture_desc.bind_flags, RHIBindFlags::DepthStencil))
                        ? RHIMemoryCategory::RenderTargetOrDepthStencil
                        : RHIMemoryCategory::Texture);
                pooled.allocation_size = device->GetResourceAllocationSize(pooled.desc, pooled.allocation_alignment);
            }

            if (pooled.allocation_size == 0)
            {
                continue;
            }

            Placement& placement = placements.emplace_back();
            placement.pooled = &pooled;
            placement.frame_resource = static_cast<FrameResourceId>(resource_index);

            placement.first_pass = frame_resource.no_cull ? 0 : frame_resource.first_pass;
            placement.last_pass = frame_resource.no_cull ? static_cast<uint32>(passes.size() - 1) : frame_resource.last_pass;
        }

        // place resources into logical heaps
        for (Heap& heap : heaps)
        {
            heap.size = 0;
            heap.alignment = 0;
        }

        std::sort(placements.begin(), placements.end(), [](const Placement& a, const Placement& b)
            {
                return a.first_pass < b.first_pass;
            });

        for (Size placement_index = 0; placement_index < placements.size(); ++placement_index)
        {
            Placement& placement = placements[placement_index];
            const PooledResource& pooled = *placement.pooled;

            placement.heap_index = heaps.size() == 1 ? 0 : static_cast<Size>(pooled.category);

            Size offset = 0;
            bool moved = true;
            while (moved)
            {
                moved = false;
                for (Size other_index = 0; other_index < placement_index; ++other_index)
                {
                    const Placement& other = placements[other_index];
                    if (other.heap_index != placement.heap_index)
                    {
                        continue;
                    }

                    if (other.last_pass < placement.first_pass || placement.last_pass < other.first_pass)
                    {
                        // no pass overlap
                        continue;
                    }

                    const Size placement_end = offset + pooled.allocation_size;
                    const Size other_end = other.heap_offset + other.pooled->allocation_size;

                    if (placement_end <= other.heap_offset || other_end <= offset)
                    {
						// no memory overlap
                        continue;
                    }

					// both lifetime and memory overlap, so this one has to go after the other
                    offset = math::Align(other_end, pooled.allocation_alignment);
                    moved = true;
                }
            }

            placement.heap_offset = offset;

			// whatever still intersects this range has a disjoint lifetime, so the two alias each other
            const Size placement_end = offset + pooled.allocation_size;
            for (Size other_index = 0; other_index < placement_index; ++other_index)
            {
                Placement& other = placements[other_index];
                if (other.heap_index != placement.heap_index)
                {
                    continue;
                }

                const Size other_end = other.heap_offset + other.pooled->allocation_size;
                if (placement_end <= other.heap_offset || other_end <= offset)
                {
                    continue;
                }

                placement.overlaps.push_back(other_index);
                other.overlaps.push_back(placement_index);
            }

            Heap& heap = heaps[placement.heap_index];
            heap.size = (std::max)(heap.size, placement_end);
            heap.alignment = (std::max)(heap.alignment, pooled.allocation_alignment);
        }

		// grow heaps that no longer fit
        Vector<bool> heap_reallocated(heaps.size(), false);
        for (Size heap_index = 0; heap_index < heaps.size(); ++heap_index)
        {
            Heap& heap = heaps[heap_index];
            if (heap.size == 0 || (heap.block && heap.block->GetSize() >= heap.size))
            {
                continue;
            }

            frame_context->RemoveResourceDeferred(std::move(heap.block));
            heap.block = device->AllocateMemory(math::Align(heap.size, heap.alignment), heap.alignment, heap.category);
            heap_reallocated[heap_index] = true;
        }

		// materialize resources
        for (const Placement& placement : placements)
        {
            PooledResource& pooled = *placement.pooled;
            RHIMemoryBlock* block = heaps[placement.heap_index].block.get();
            if (!block)
            {
                continue;
            }

            if (!pooled.resource
                || pooled.heap_index != placement.heap_index
                || pooled.heap_offset != placement.heap_offset
                || heap_reallocated[placement.heap_index])
            {
                frame_context->RemoveResourceDeferred(std::move(pooled.resource));
                pooled.resource = pooled.desc.type == RHIResourceType::Buffer
                    ? device->CreatePlacedBuffer(*block, placement.heap_offset, pooled.desc.buffer_desc)
                    : device->CreatePlacedTexture(*block, placement.heap_offset, pooled.desc.texture_desc);
                if (!pooled.resource)
                {
                    continue;
                }
                pooled.resource->SetName(pooled.name + " (View " + std::to_string(pooled.scope) + ")");
                pooled.state = RHIResourceState::Undefined;

                for (PooledSubresource& subresource : pooled.subresources)
                {
                    subresource.realized = false;
                }
            }

            pooled.heap_index = placement.heap_index;
            pooled.heap_offset = placement.heap_offset;

            for (PooledSubresource& subresource : pooled.subresources)
            {
                if (subresource.realized)
                {
                    continue;
                }

                subresource.realized = device->UpdateSubresource(*pooled.resource, subresource.desc, subresource.handle);
            }

            FrameResource& frame_resource = frame_resources[placement.frame_resource];
            frame_resource.resource = pooled.resource.get();

            frame_resource.state = pooled.state;
        }

		// command lists are opened in pass order so submission follows the graph order
        for (Size pass_index = 0; pass_index < passes.size(); ++pass_index)
        {
            Pass& pass = passes[pass_index];
            if (!pass.alive)
            {
                continue;
            }

            pass.command_list = frame_context->BeginCommandList(*device);
            if (!pass.command_list)
            {
                backlog::Post((String("FrameGraph: no command list for pass '") + pass.name + "', the rest of the frame is dropped").c_str(), backlog::LogLevel::Error);
                break;
            }

            for (const Placement& placement : placements)
            {
                if (placement.first_pass != pass_index || placement.overlaps.empty())
                {
                    continue;
                }

                FrameResource& frame_resource = frame_resources[placement.frame_resource];
                if (!frame_resource.resource)
                {
                    continue;
                }

                for (Size overlap_index : placement.overlaps)
                {
                    const Placement& overlap = placements[overlap_index];
                    if (overlap.last_pass >= placement.first_pass)
                    {
                        continue;
                    }

                    pass.command_list->AliasingBarrier(overlap.pooled->resource.get(), *frame_resource.resource);
                }

                for (const FrameResourceAccess& access : pass.accesses)
                {
                    if (access.resource == placement.frame_resource && access.type == FrameResourceAccess::Type::Read)
                    {
                        backlog::Post((String("FrameGraph: '") + frame_resource.name + "' aliases memory but its first access reads it").c_str(), backlog::LogLevel::Warning);
                    }
                }
            }

            for (const FrameResourceAccess& access : pass.accesses)
            {
                if (access.resource >= frame_resources.size())
                {
                    continue;
                }

                FrameResource& frame_resource = frame_resources[access.resource];
                if (!frame_resource.resource || frame_resource.state == access.state)
                {
                    continue;
                }

                pass.command_list->TransitionResource(*frame_resource.resource, frame_resource.state, access.state);
                frame_resource.state = access.state;
            }
        }

		// carry the states the passes left behind over to the next frame
        for (const Placement& placement : placements)
        {
            placement.pooled->state = frame_resources[placement.frame_resource].state;
        }

        RHICommandList* epilogue_command_list = frame_context->BeginCommandList(*device);
        if (epilogue_command_list)
        {
			epilogue_command_list->BeginEvent("FrameGraph Epilogue");

            for (FrameResource& frame_resource : frame_resources)
            {
                if (frame_resource.transient
                    || !frame_resource.alive
                    || !frame_resource.resource
                    || frame_resource.state == frame_resource.entry_state)
                {
                    continue;
                }

                epilogue_command_list->TransitionResource(*frame_resource.resource, frame_resource.state, frame_resource.entry_state);
                frame_resource.state = frame_resource.entry_state;
            }

            epilogue_command_list->EndEvent();
        }

    }

    void FrameGraph::Dispatch(jobsystem::Context& context)
    {
        const FrameResource* resources_data = frame_resources.data();
        const Size resources_count = frame_resources.size();

        for (Pass& pass : passes)
        {
            if (!pass.alive || !pass.execute || !pass.command_list)
            {
                continue;
            }

            jobsystem::Execute(context, [pass = &pass, resources_data, resources_count](jobsystem::JobArgs)
            {
                FrameGraphPassContext pass_context = {};
                pass_context.command_list = pass->command_list;
                pass_context.frame_resources = resources_data;
                pass_context.frame_resource_count = resources_count;

                pass->command_list->BeginEvent(pass->name.c_str());
                pass->execute(pass_context);
                pass->command_list->EndEvent();
            });
        }
    }
}
