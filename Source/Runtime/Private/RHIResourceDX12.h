#pragma once

#include "RHIResource.h"
#include "D3D12MemoryAllocator/D3D12MemAlloc.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Resource;

namespace won::rendering
{
    class RHIResourceDX12 final : public RHIResource
    {
    public:
        RHIResourceDX12(const RHIResourceDesc& desc, ComPtr<ID3D12Resource> resource_in,
            D3D12MA::Allocation* allocation_in);
        ~RHIResourceDX12() override;

        const RHIResourceDesc& GetDesc() const override;
        void SetName(const String& new_name) override;
        const String& GetName() const override;

        ID3D12Resource* GetResource() const;
        void* GetMappedData() const;

    private:
        RHIResourceDesc desc = {};
        String name;
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        void* mapped_data = nullptr;
    };
}
