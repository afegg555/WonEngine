#include "RHIResourceDX12.h"
#include "DescriptorAllocatorDX12.h"
#include "RHIFormatDX12.h"

#include <Windows.h>

namespace won::rendering
{
    namespace resource_dx12
    {
        DXGI_FORMAT ToNative(RHIFormat format, NativeFormatUsage usage)
        {
            switch (usage)
            {
            case NativeFormatUsage::Resource: return ToDXGIResourceFormat(format);
            case NativeFormatUsage::DepthStencil: return ToDXGIDsvFormat(format);
            case NativeFormatUsage::ShaderResource: return ToDXGISrvFormat(format);
            case NativeFormatUsage::UnorderedAccess: return ToDXGIUavFormat(format);
            default: return ToDXGIFormat(format);
            }
        }

        D3D12_RESOURCE_DESC ToNative(const RHIBufferDesc& desc)
        {
            D3D12_RESOURCE_DESC resource_desc = {};
            resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
            resource_desc.Width = desc.size;
            resource_desc.Height = 1;
            resource_desc.DepthOrArraySize = 1;
            resource_desc.MipLevels = 1;
            resource_desc.Format = DXGI_FORMAT_UNKNOWN;
            resource_desc.SampleDesc.Count = 1;
            resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
            resource_desc.Flags = HasBindFlag(desc.bind_flags, RHIBindFlags::UnorderedAccess)
                ? D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS : D3D12_RESOURCE_FLAG_NONE;
            return resource_desc;
        }

        D3D12_RESOURCE_DESC ToNative(const RHITextureDesc& desc)
        {
            D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
            if (HasBindFlag(desc.bind_flags, RHIBindFlags::RenderTarget))
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
            }
            if (HasBindFlag(desc.bind_flags, RHIBindFlags::DepthStencil))
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
            }
            if (HasBindFlag(desc.bind_flags, RHIBindFlags::UnorderedAccess))
            {
                flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
            }

            D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            uint16 depth_or_array_size = static_cast<uint16>(desc.array_layers);
            if (desc.depth > 1)
            {
                dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
                depth_or_array_size = static_cast<uint16>(desc.depth);
            }
            else if (desc.is_cube)
            {
                depth_or_array_size = 6;
            }

            D3D12_RESOURCE_DESC resource_desc = {};
            resource_desc.Dimension = dimension;
            resource_desc.Width = desc.width;
            resource_desc.Height = desc.height;
            resource_desc.DepthOrArraySize = depth_or_array_size;
            resource_desc.MipLevels = static_cast<uint16>(desc.mip_levels);
            resource_desc.Format = ToDXGIResourceFormat(desc.format);
            resource_desc.SampleDesc.Count = desc.sample_count;
            resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            resource_desc.Flags = flags;
            return resource_desc;
        }

        D3D12_RESOURCE_DESC ToNative(const RHIResourceDesc& desc)
        {
            return desc.type == RHIResourceType::Buffer
                ? ToNative(desc.buffer_desc)
                : ToNative(desc.texture_desc);
        }

        D3D12_HEAP_FLAGS ToNative(RHIMemoryCategory category)
        {
            switch (category)
            {
            case RHIMemoryCategory::Buffer: return D3D12_HEAP_FLAG_ALLOW_ONLY_BUFFERS;
            case RHIMemoryCategory::RenderTargetOrDepthStencil: return D3D12_HEAP_FLAG_ALLOW_ONLY_RT_DS_TEXTURES;
            default: return D3D12_HEAP_FLAG_ALLOW_ONLY_NON_RT_DS_TEXTURES;
            }
        }
    }

    RHIMemoryBlockDX12::RHIMemoryBlockDX12(D3D12MA::Allocation* allocation_in, Size size_in)
        : allocation(allocation_in)
        , size(size_in)
    {
    }

    RHIMemoryBlockDX12::~RHIMemoryBlockDX12()
    {
        if (allocation)
        {
            allocation->Release();
            allocation = nullptr;
        }
    }

    void RHIMemoryBlockDX12::SetName(const String& new_name)
    {
        name = new_name;
    }

    const String& RHIMemoryBlockDX12::GetName() const
    {
        return name;
    }

    Size RHIMemoryBlockDX12::GetSize() const
    {
        return size;
    }

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
        DescriptorAllocatorDX12* descriptor_allocator_in)
        : desc(desc_in)
        , resource(std::move(resource_in))
        , allocation(allocation_in)
        , descriptor_allocator(descriptor_allocator_in)
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

    bool RHIResourceDX12::Replace(ComPtr<ID3D12Resource> new_resource, D3D12_RESOURCE_STATES new_state, std::unique_ptr<RHIResource>& out_retired)
    {
        if (!new_resource || mapped_data)
        {
            return false;
        }

        auto retired = std::make_unique<RHIResourceDX12>(desc, std::move(resource), allocation, nullptr);
        retired->SetName(name);

        resource = std::move(new_resource);
        allocation = nullptr;
        current_state = new_state;
        SetName(name);

        for (SubresourceEntry& entry : subresources)
        {
            if (!entry.valid || entry.descriptor_index < 0)
            {
                continue;
            }

            if (!descriptor_allocator->UpdateSubresourceDescriptor(*this, entry.desc, entry.descriptor_index))
            {
                return false;
            }
        }

        out_retired = std::move(retired);
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
