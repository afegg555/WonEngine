#pragma once
#include "RHISampler.h"

#include "DirectX-Headers/d3d12.h"

#include <memory>

namespace won::rendering
{
    class DescriptorAllocatorDX12;

    class RHISamplerDX12 final : public RHISampler
    {
    public:
        RHISamplerDX12(const RHISamplerDesc& desc_in, int descriptor_index_in,
            std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in);
        ~RHISamplerDX12() override;

        void SetName(const String& new_name) override;
        const String& GetName() const override;

        const RHISamplerDesc& GetDesc() const;
        int GetDescriptorIndex() const;

    private:
        RHISamplerDesc desc = {};
        String name;
        int descriptor_index = -1;
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator = {};
    };
}
