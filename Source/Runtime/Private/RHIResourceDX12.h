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

    namespace resource_dx12
    {
        enum class NativeFormatUsage
        {
            Typed,
            Resource,
            DepthStencil,
            ShaderResource,
            UnorderedAccess,
        };

        DXGI_FORMAT ToNative(RHIFormat format, NativeFormatUsage usage);
        D3D12_RESOURCE_DESC ToNative(const RHIBufferDesc& desc);
        D3D12_RESOURCE_DESC ToNative(const RHITextureDesc& desc);
        D3D12_RESOURCE_DESC ToNative(const RHIResourceDesc& desc);
        D3D12_HEAP_FLAGS ToNative(RHIMemoryCategory category);
    }

    class RHIMemoryBlockDX12 final : public RHIMemoryBlock
    {
    public:
        RHIMemoryBlockDX12(D3D12MA::Allocation* allocation_in, Size size_in);
        ~RHIMemoryBlockDX12() override;

        void SetName(const String& new_name) override;
        const String& GetName() const override;
        Size GetSize() const override;

        D3D12MA::Allocation* GetAllocation() const { return allocation; }

    private:
        D3D12MA::Allocation* allocation = nullptr;
        Size size = 0;
        String name;
    };

    class RHIResourceDX12 final : public RHIResource
    {
    public:
        RHIResourceDX12(const RHIResourceDesc& desc, ComPtr<ID3D12Resource> resource_in,
            D3D12MA::Allocation* allocation_in,
            DescriptorAllocatorDX12* descriptor_allocator_in);
        ~RHIResourceDX12() override;

        const RHIResourceDesc& GetDesc() const override;
        void SetName(const String& new_name) override;
        const String& GetName() const override;

        ID3D12Resource* GetResource() const;
        void* GetMappedData() const override;

        bool FindSubresource(const RHISubresourceDesc& desc, RHISubresourceHandle* out_handle) const;
        bool CreateSubresource(const RHISubresourceDesc& desc, RHISubresourceHandle* out_handle);
        bool Replace(ComPtr<ID3D12Resource> new_resource, std::unique_ptr<RHIResource>& out_retired);

    private:
        friend class RHICommandListDX12;

        struct SubresourceEntry
        {
            RHISubresourceDesc desc = {};
            int descriptor_index = -1;
            bool valid = false;
        };

        const SubresourceEntry* FindSubresourceEntry(const RHISubresourceHandle& handle) const;

        RHIResourceDesc desc = {};
        String name;
        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        DescriptorAllocatorDX12* descriptor_allocator = nullptr;
        void* mapped_data = nullptr;
        Vector<SubresourceEntry> subresources;
    };
}
