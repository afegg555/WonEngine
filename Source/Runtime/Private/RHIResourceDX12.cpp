#include "RHIResourceDX12.h"
#include "DescriptorAllocatorDX12.h"

namespace won::rendering
{
    RHIResourceDX12::RHIResourceDX12(const RHIResourceDesc& desc_in, ComPtr<ID3D12Resource> resource_in,
        D3D12MA::Allocation* allocation_in,
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in)
        : desc(desc_in)
        , resource(std::move(resource_in))
        , allocation(allocation_in)
        , descriptor_allocator(std::move(descriptor_allocator_in))
    {
        if (resource && desc.type == RHIResourceType::Buffer)
        {
            if (desc.buffer_desc.usage == RHIResourceUsage::Upload)
            {
                D3D12_RANGE read_range = { 0, 0 };
                if (FAILED(resource->Map(0, &read_range, &mapped_data))) // no read
                {
                    mapped_data = nullptr;
                }
            }
            else if (desc.buffer_desc.usage == RHIResourceUsage::Readback)
            {
                if (FAILED(resource->Map(0, nullptr, &mapped_data))) // nullptr means whole size
                {
                    mapped_data = nullptr;
                }
            }
        }
    }

    RHIResourceDX12::~RHIResourceDX12()
    {
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_shared = descriptor_allocator.lock();
        if (descriptor_allocator_shared)
        {
            for (const auto& entry : subresources)
            {
                if (entry.valid && entry.heap_type != D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES && entry.descriptor_index != 0xffffffffu)
                {
                    descriptor_allocator_shared->ReleaseDescriptor(entry.heap_type, entry.descriptor_index);
                }
            }
        }

        if (resource && mapped_data)
        {
            resource->Unmap(0, nullptr);
            mapped_data = nullptr;
        }

        resource.Reset();
        if (allocation)
        {
            allocation->Release();
            allocation = nullptr;
        }
    }

    const RHIResourceDesc& RHIResourceDX12::GetDesc() const
    {
        return desc;
    }

    void RHIResourceDX12::SetName(const String& new_name)
    {
        name = new_name;
    }

    const String& RHIResourceDX12::GetName() const
    {
        return name;
    }

    ID3D12Resource* RHIResourceDX12::GetResource() const
    {
        return resource.Get();
    }

    void* RHIResourceDX12::GetMappedData() const
    {
        return mapped_data;
    }

    void RHIResourceDX12::SetCurrentState(D3D12_RESOURCE_STATES new_state)
    {
        current_state = new_state;
    }

    D3D12_RESOURCE_STATES RHIResourceDX12::GetCurrentState() const
    {
        return current_state;
    }

    bool RHIResourceDX12::FindSubresource(const RHISubresourceDesc& desc_in, RHISubresourceHandle* out_handle) const
    {
        if (!out_handle)
        {
            return false;
        }

        for (uint32 i = 0; i < static_cast<uint32>(subresources.size()); ++i)
        {
            const SubresourceEntry& entry = subresources[i];
            if (!entry.valid)
            {
                continue;
            }

            if (IsSameSubresourceDesc(entry.desc, desc_in))
            {
                out_handle->index = i;
                return true;
            }
        }

        return false;
    }

    bool RHIResourceDX12::AddSubresource(const RHISubresourceDesc& desc_in,
        D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        uint32 descriptor_index,
        RHISubresourceHandle* out_handle)
    {
        if (!out_handle)
        {
            return false;
        }

        SubresourceEntry entry = {};
        entry.desc = desc_in;
        entry.heap_type = heap_type;
        entry.descriptor_index = descriptor_index;
        entry.valid = true;

        subresources.push_back(entry);
        out_handle->index = static_cast<uint32>(subresources.size() - 1);
        return true;
    }

    bool RHIResourceDX12::GetSubresourceDescriptor(const RHISubresourceHandle& handle,
        D3D12_DESCRIPTOR_HEAP_TYPE& out_heap_type,
        uint32& out_descriptor_index) const
    {
        if (!handle.IsValid() || handle.index >= static_cast<uint32>(subresources.size()))
        {
            return false;
        }

        const SubresourceEntry& entry = subresources[handle.index];
        if (!entry.valid)
        {
            return false;
        }

        if (entry.heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES || entry.descriptor_index == ~0u)
        {
            return false;
        }

        out_heap_type = entry.heap_type;
        out_descriptor_index = entry.descriptor_index;
        return true;
    }

    bool RHIResourceDX12::IsSameSubresourceDesc(const RHISubresourceDesc& lhs, const RHISubresourceDesc& rhs) const
    {
        return lhs.type == rhs.type &&
            lhs.format == rhs.format &&
            lhs.first_slice == rhs.first_slice &&
            lhs.slice_count == rhs.slice_count &&
            lhs.first_mip == rhs.first_mip &&
            lhs.mip_count == rhs.mip_count &&
            lhs.buffer_offset == rhs.buffer_offset &&
            lhs.buffer_size == rhs.buffer_size &&
            lhs.buffer_stride == rhs.buffer_stride;
    }
}
