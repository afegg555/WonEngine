#include "FrameGraphResourcePool.h"

#include "RHIDevice.h"

namespace won::rendering
{
    void FrameGraphResourcePool::BeginFrame(RHIDevice& in_device, FrameContext& in_frame_context)
    {
        device = &in_device;
        frame_context = &in_frame_context;

		// remove resources that have been unused for more than a few frames
        for (Size pool_index = pool.size(); pool_index > 0; --pool_index)
        {
            PooledResource& entry = pool[pool_index - 1];
            ++entry.unused_frames;
            if (entry.unused_frames <= max_frames_in_flight)
            {
                continue;
            }

            frame_context->RemoveResourceDeferred(entry.resource);
            pool.erase(pool.begin() + (pool_index - 1));
        }
    }

    RHIResource* FrameGraphResourcePool::Create(uint32 scope, const char* name, const RHIResourceDesc& desc)
    {
        PooledResource* entry = nullptr;
        for (PooledResource& pooled : pool)
        {
            if (pooled.scope == scope && pooled.name == name)
            {
                entry = &pooled;
                break;
            }
        }

        if (!entry)
        {
            entry = &pool.emplace_back();
            entry->scope = scope;
            entry->name = name;
        }
        entry->unused_frames = 0; // reset

        bool desc_changed = entry->desc.type != desc.type;
        if (desc.type == RHIResourceType::Buffer)
        {
            desc_changed = desc_changed
                || entry->desc.buffer_desc.size < desc.buffer_desc.size // we can reuse a larger buffer
                || entry->desc.buffer_desc.usage != desc.buffer_desc.usage
                || entry->desc.buffer_desc.bind_flags != desc.buffer_desc.bind_flags
                || entry->desc.buffer_desc.misc_flags != desc.buffer_desc.misc_flags;
        }
        else
        {
            const RHITextureDesc& cached = entry->desc.texture_desc;
            const RHITextureDesc& wanted = desc.texture_desc;
            desc_changed = desc_changed
                || cached.width != wanted.width // texture size is part of how shaders address it, so it has to match exactly
                || cached.height != wanted.height
                || cached.depth != wanted.depth
                || cached.mip_levels != wanted.mip_levels
                || cached.array_layers != wanted.array_layers
                || cached.sample_count != wanted.sample_count
                || cached.is_cube != wanted.is_cube
                || cached.format != wanted.format
                || cached.usage != wanted.usage
                || cached.bind_flags != wanted.bind_flags
                || cached.misc_flags != wanted.misc_flags
                || cached.clear_color[0] != wanted.clear_color[0]
                || cached.clear_color[1] != wanted.clear_color[1]
                || cached.clear_color[2] != wanted.clear_color[2]
                || cached.clear_color[3] != wanted.clear_color[3];
        }

        if (!entry->resource || desc_changed)
        {
            frame_context->RemoveResourceDeferred(entry->resource);
            entry->subresources.clear();
            entry->desc = desc;
            entry->resource = desc.type == RHIResourceType::Buffer
                ? device->CreateBuffer(desc.buffer_desc)
                : device->CreateTexture(desc.texture_desc);
            if (!entry->resource)
            {
                return nullptr;
            }
            entry->resource->SetName(name);
        }

        return entry->resource.get();
    }

    RHIResource* FrameGraphResourcePool::CreateBuffer(uint32 scope, const char* name, const RHIBufferDesc& desc)
    {
        RHIResourceDesc resource_desc = {};
        resource_desc.type = RHIResourceType::Buffer;
        resource_desc.buffer_desc = desc;
        return Create(scope, name, resource_desc);
    }

    RHIResource* FrameGraphResourcePool::CreateTexture(uint32 scope, const char* name, const RHITextureDesc& desc)
    {
        RHIResourceDesc resource_desc = {};
        resource_desc.type = RHIResourceType::Texture2D;
        resource_desc.texture_desc = desc;
        return Create(scope, name, resource_desc);
    }

    RHISubresourceHandle FrameGraphResourcePool::GetSubresource(RHIResource& resource, const RHISubresourceDesc& desc)
    {
        PooledResource* entry = nullptr;
        for (PooledResource& pooled : pool)
        {
            if (pooled.resource.get() == &resource)
            {
                entry = &pooled;
                break;
            }
        }

        if (!entry)
        {
            return {};
        }

        for (const PooledSubresource& subresource : entry->subresources)
        {
            if (subresource.desc.type == desc.type
                && subresource.desc.format == desc.format
                && subresource.desc.first_slice == desc.first_slice
                && subresource.desc.slice_count == desc.slice_count
                && subresource.desc.first_mip == desc.first_mip
                && subresource.desc.mip_count == desc.mip_count
                && subresource.desc.buffer_offset == desc.buffer_offset
                && subresource.desc.buffer_size == desc.buffer_size
                && subresource.desc.buffer_stride == desc.buffer_stride)
            {
                return subresource.handle;
            }
        }

        RHISubresourceHandle handle = {};
        if (!device->CreateSubresource(resource, desc, &handle))
        {
            return {};
        }

        PooledSubresource& created = entry->subresources.emplace_back();
        created.desc = desc;
        created.handle = handle;
        return handle;
    }
}
