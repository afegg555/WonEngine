#include "RHIResourceDX12.h"

namespace won::rendering
{
    RHIResourceDX12::RHIResourceDX12(const RHIResourceDesc& desc_in, ComPtr<ID3D12Resource> resource_in,
        D3D12MA::Allocation* allocation_in)
        : desc(desc_in)
        , resource(std::move(resource_in))
        , allocation(allocation_in)
    {
        if (resource && desc.type != RHIResourceType::Buffer)
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
}
