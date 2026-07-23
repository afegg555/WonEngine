#include "RHIResourceDX12.h"
#include "DescriptorAllocatorDX12.h"

#include <Windows.h>

namespace won::rendering
{
    namespace
    {
        D3D12_DESCRIPTOR_HEAP_TYPE ConvertSubresourceToHeapType(RHISubresourceType subresource_type)
        {
            switch (subresource_type)
            {
            case won::rendering::RHISubresourceType::ConstantBuffer:
            case won::rendering::RHISubresourceType::ShaderResource:
            case won::rendering::RHISubresourceType::UnorderedAccess:
                return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            case won::rendering::RHISubresourceType::RenderTarget:
                return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            case won::rendering::RHISubresourceType::DepthStencil:
                return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
            case won::rendering::RHISubresourceType::Unknown:
            case won::rendering::RHISubresourceType::VertexBuffer:
            case won::rendering::RHISubresourceType::IndexBuffer:
            default:
                return D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
            }
        }
    }

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
        if (descriptor_allocator)
        {
            for (const auto& entry : subresources)
            {
                if (entry.valid&& entry.descriptor_index >= 0)
                {
                    auto heap_type = ConvertSubresourceToHeapType(entry.desc.type);
                    descriptor_allocator->ReleaseDescriptor(heap_type, entry.descriptor_index);
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
        if (!resource)
        {
            return;
        }

        const int wide_length = MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0);
        if (wide_length <= 1)
        {
            resource->SetName(L"");
            return;
        }

        WString wide_name(static_cast<Size>(wide_length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wide_name.data(), wide_length) <= 0)
        {
            return;
        }

        resource->SetName(wide_name.c_str());
        if (allocation)
        {
            allocation->SetName(wide_name.c_str());
        }
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

        for (const auto& entry : subresources)
        {
            if (!entry.valid)
            {
                continue;
            }

            if (IsSameSubresourceDesc(entry.desc, desc_in))
            {
                out_handle->descriptor_index = entry.descriptor_index;
                return true;
            }
        }

        return false;
    }

    bool RHIResourceDX12::CreateSubresource(const RHISubresourceDesc& desc_in, RHISubresourceHandle* out_handle)
    {
        if (!out_handle)
        {
            return false;
        }
        D3D12_DESCRIPTOR_HEAP_TYPE out_heap_type{};
        int descriptor_index = -1;
        if (!descriptor_allocator->CreateSubresourceDescriptor(*this, desc_in, out_heap_type, descriptor_index))
        {
            return false;
        }

        SubresourceEntry entry = {};
        entry.desc = desc_in;
        entry.descriptor_index = descriptor_index;
        entry.valid = true;

        subresources.push_back(entry);
        out_handle->descriptor_index = descriptor_index;
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

    const RHIResourceDX12::SubresourceEntry* RHIResourceDX12::FindSubresourceEntry(const RHISubresourceHandle& handle) const
    {
        for (const auto& entry : subresources)
        {
            if (!entry.valid || entry.descriptor_index != handle.descriptor_index)
            {
                continue;
            }

            return &entry;
        }

        return nullptr;
    }
}
