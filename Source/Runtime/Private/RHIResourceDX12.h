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
        void* GetMappedData() const;
        void SetCurrentState(D3D12_RESOURCE_STATES new_state);
        D3D12_RESOURCE_STATES GetCurrentState() const;
        bool FindSubresource(const RHISubresourceDesc& desc, RHISubresourceHandle* out_handle) const;
        bool AddSubresource(const RHISubresourceDesc& desc,
            D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
            uint32 descriptor_index,
            RHISubresourceHandle* out_handle);
        bool GetSubresourceDescriptor(const RHISubresourceHandle& handle,
            D3D12_DESCRIPTOR_HEAP_TYPE& out_heap_type,
            uint32& out_descriptor_index) const;

    private:
        struct SubresourceEntry
        {
            RHISubresourceDesc desc = {};
            D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
            uint32 descriptor_index = ~0u;
            bool valid = false;
        };

        bool IsSameSubresourceDesc(const RHISubresourceDesc& lhs, const RHISubresourceDesc& rhs) const;

        RHIResourceDesc desc = {};
        String name;
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        std::weak_ptr<DescriptorAllocatorDX12> descriptor_allocator;
        void* mapped_data = nullptr;
        D3D12_RESOURCE_STATES current_state = D3D12_RESOURCE_STATE_COMMON;
        Vector<SubresourceEntry> subresources;
    };
}
