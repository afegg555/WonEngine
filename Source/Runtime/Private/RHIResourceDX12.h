#pragma once
#include "RHIResource.h"
#include "DirectX-Headers/d3d12.h"
#include <dxgi1_6.h>
#define D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include "D3D12MemoryAllocator/D3D12MemAlloc.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Resource;

namespace won::rendering
{
    class DescriptorAllocatorDX12;
    class RHICommandListDX12;

    class RHIResourceDX12 final : public RHIResource
    {
    public:
        RHIResourceDX12(const RHIResourceDesc& desc, ComPtr<ID3D12Resource> resource_in,
            D3D12MA::Allocation* allocation_in,
            std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in);
        ~RHIResourceDX12() override;

        const RHIResourceDesc& GetDesc() const override;
        void SetName(const String& new_name) override;
        const String& GetName() const override;

        ID3D12Resource* GetResource() const;
        void* GetMappedData() const override;

        void SetCurrentState(D3D12_RESOURCE_STATES new_state);
        D3D12_RESOURCE_STATES GetCurrentState() const;

        bool FindSubresource(const RHISubresourceDesc& desc, RHISubresourceHandle* out_handle) const;
        bool CreateSubresource(const RHISubresourceDesc& desc, RHISubresourceHandle* out_handle);

    private:
        friend class RHICommandListDX12;

        struct SubresourceEntry
        {
            RHISubresourceDesc desc = {};
            int descriptor_index = -1;
            bool valid = false;
        };

        bool IsSameSubresourceDesc(const RHISubresourceDesc& lhs, const RHISubresourceDesc& rhs) const;
        const SubresourceEntry* FindSubresourceEntry(const RHISubresourceHandle& handle) const;

        RHIResourceDesc desc = {};
        String name;
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator;
        void* mapped_data = nullptr;
        D3D12_RESOURCE_STATES current_state = D3D12_RESOURCE_STATE_COMMON;
        Vector<SubresourceEntry> subresources;
    };
}
