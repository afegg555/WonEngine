#include "RHISamplerDX12.h"

#include "DescriptorAllocatorDX12.h"

namespace won::rendering
{
    RHISamplerDX12::RHISamplerDX12(const RHISamplerDesc& desc_in, int descriptor_index_in,
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in)
        : desc(desc_in)
        , descriptor_index(descriptor_index_in)
        , descriptor_allocator(std::move(descriptor_allocator_in))
    {
    }

    RHISamplerDX12::~RHISamplerDX12()
    {
        if (descriptor_allocator && descriptor_index >= 0)
        {
            descriptor_allocator->ReleaseDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, descriptor_index);
        }
    }

    void RHISamplerDX12::SetName(const String& new_name)
    {
        name = new_name;
    }

    const String& RHISamplerDX12::GetName() const
    {
        return name;
    }

    const RHISamplerDesc& RHISamplerDX12::GetDesc() const
    {
        return desc;
    }

    int RHISamplerDX12::GetDescriptorIndex() const
    {
        return descriptor_index;
    }
}
