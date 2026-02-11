#include "DescriptorAllocatorDX12.h"

#include "Backlog.h"
#include "RHIFormatDX12.h"
#include "RHIResourceDX12.h"

namespace won::rendering
{
    namespace
    {
        constexpr uint32 kRtvMasterDescriptorCount = 4096;
        constexpr uint32 kDsvMasterDescriptorCount = 2048;
        constexpr uint32 kCbvSrvUavMasterDescriptorCount = 1000000;
        constexpr uint32 kSamplerMasterDescriptorCount = 2048;
        constexpr uint32 kCbvSrvUavFrameDescriptorCount = 8192;
        constexpr uint32 kSamplerFrameDescriptorCount = 1024;

        UINT AlignConstantBufferSize(UINT size)
        {
            return (size + 255u) & ~255u;
        }
    }

    DescriptorAllocatorDX12::DescriptorAllocatorDX12(ComPtr<ID3D12Device> device_in)
        : device(std::move(device_in))
    {
        if (!device)
        {
            backlog::Post("DescriptorAllocatorDX12 requires valid DX12 device", backlog::LogLevel::Error);
            return;
        }

        rtv_master_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        dsv_master_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        cbv_srv_uav_master_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sampler_master_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
        cbv_srv_uav_frame_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sampler_frame_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

        if (!CreateDescriptorHeap(rtv_master_heap, kRtvMasterDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(dsv_master_heap, kDsvMasterDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(cbv_srv_uav_master_heap, kCbvSrvUavMasterDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(sampler_master_heap, kSamplerMasterDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(cbv_srv_uav_frame_heap, kCbvSrvUavFrameDescriptorCount, true))
        {
            return;
        }
        if (!CreateDescriptorHeap(sampler_frame_heap, kSamplerFrameDescriptorCount, true))
        {
            return;
        }
    }

    bool DescriptorAllocatorDX12::IsValid() const
    {
        return device &&
            rtv_master_heap.heap &&
            dsv_master_heap.heap &&
            cbv_srv_uav_master_heap.heap &&
            sampler_master_heap.heap &&
            cbv_srv_uav_frame_heap.heap &&
            sampler_frame_heap.heap;
    }

    void DescriptorAllocatorDX12::BeginFrame()
    {
        frame_cbv_srv_uav_count = 0;
        frame_sampler_count = 0;
    }

    bool DescriptorAllocatorDX12::CreateSubresourceDescriptor(RHIResourceDX12& resource,
        const RHISubresourceDesc& desc,
        D3D12_DESCRIPTOR_HEAP_TYPE& out_heap_type,
        uint32& out_descriptor_index)
    {
        if (!resource.GetResource())
        {
            return false;
        }

        DescriptorHeap* target_heap = nullptr;
        switch (desc.type)
        {
        case RHISubresourceType::RenderTarget:
            target_heap = &rtv_master_heap;
            break;
        case RHISubresourceType::DepthStencil:
            target_heap = &dsv_master_heap;
            break;
        case RHISubresourceType::ConstantBuffer:
        case RHISubresourceType::ShaderResource:
        case RHISubresourceType::UnorderedAccess:
            target_heap = &cbv_srv_uav_master_heap;
            break;
        default:
            backlog::Post("Unsupported subresource type", backlog::LogLevel::Error);
            return false;
        }

        uint32 descriptor_index;
        if (!AllocateFromHeap(*target_heap, descriptor_index))
        {
            backlog::Post("Descriptor heap allocation failed", backlog::LogLevel::Error);
            return false;
        }

        const D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = GetCpuHandle(*target_heap, descriptor_index);
        bool created = false;
        switch (desc.type)
        {
        case RHISubresourceType::RenderTarget:
            created = CreateRenderTargetView(resource, desc, cpu_handle);
            break;
        case RHISubresourceType::DepthStencil:
            created = CreateDepthStencilView(resource, desc, cpu_handle);
            break;
        case RHISubresourceType::ShaderResource:
            created = CreateShaderResourceView(resource, desc, cpu_handle);
            break;
        case RHISubresourceType::UnorderedAccess:
            created = CreateUnorderedAccessView(resource, desc, cpu_handle);
            break;
        case RHISubresourceType::ConstantBuffer:
            created = CreateConstantBufferView(resource, desc, cpu_handle);
            break;
        default:
            created = false;
            break;
        }

        if (!created)
        {
            FreeToHeap(*target_heap, descriptor_index);
            return false;
        }

        out_heap_type = target_heap->heap_type;
        out_descriptor_index = descriptor_index;
        return true;
    }

    bool DescriptorAllocatorDX12::GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        uint32 descriptor_index,
        D3D12_CPU_DESCRIPTOR_HANDLE& out_handle) const
    {
        const DescriptorHeap* des_heap = GetDescriptorHeap(heap_type);
        if (!des_heap || !des_heap->heap || descriptor_index >= des_heap->allocated_count)
        {
            return false;
        }

        out_handle = GetCpuHandle(*des_heap, descriptor_index);
        return true;
    }

    void DescriptorAllocatorDX12::ReleaseDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, uint32 descriptor_index)
    {
        DescriptorHeap* des_heap = GetDescriptorHeap(heap_type);
        if (!des_heap || !des_heap->heap || descriptor_index >= des_heap->allocated_count)
        {
            return;
        }

        FreeToHeap(*des_heap, descriptor_index);
    }

    bool DescriptorAllocatorDX12::CopyToFrameHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        D3D12_CPU_DESCRIPTOR_HANDLE source_handle,
        uint32& out_bindless_index,
        D3D12_GPU_DESCRIPTOR_HANDLE& out_gpu_handle)
    {
        DescriptorHeap* frame_heap = nullptr;
        uint32* frame_count = nullptr;
        if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        {
            frame_heap = &cbv_srv_uav_frame_heap;
            frame_count = &frame_cbv_srv_uav_count;
        }
        else if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
        {
            frame_heap = &sampler_frame_heap;
            frame_count = &frame_sampler_count;
        }
        else
        {
            return false;
        }

        if (!frame_heap || !frame_count || *frame_count >= frame_heap->capacity)
        {
            backlog::Post("Frame descriptor heap is full", backlog::LogLevel::Error);
            return false;
        }

        const uint32 frame_descriptor_index = *frame_count;
        const D3D12_CPU_DESCRIPTOR_HANDLE destination_handle = GetCpuHandle(*frame_heap, frame_descriptor_index);
        device->CopyDescriptorsSimple(1, destination_handle, source_handle, heap_type);

        out_bindless_index = frame_descriptor_index;
        out_gpu_handle = GetGpuHandle(*frame_heap, frame_descriptor_index);
        ++(*frame_count);
        return true;
    }

    ID3D12DescriptorHeap* DescriptorAllocatorDX12::GetFrameCbvSrvUavHeap() const
    {
        return cbv_srv_uav_frame_heap.heap.Get();
    }

    ID3D12DescriptorHeap* DescriptorAllocatorDX12::GetFrameSamplerHeap() const
    {
        return sampler_frame_heap.heap.Get();
    }

    DescriptorAllocatorDX12::DescriptorHeap* DescriptorAllocatorDX12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type)
    {
        switch (heap_type)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: return &rtv_master_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: return &dsv_master_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return &cbv_srv_uav_master_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: return &sampler_master_heap;
        default: return nullptr;
        }
    }

    const DescriptorAllocatorDX12::DescriptorHeap* DescriptorAllocatorDX12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type) const
    {
        switch (heap_type)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: return &rtv_master_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: return &dsv_master_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return &cbv_srv_uav_master_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: return &sampler_master_heap;
        default: return nullptr;
        }
    }

    bool DescriptorAllocatorDX12::CreateDescriptorHeap(DescriptorHeap& heap, uint32 capacity, bool shader_visible) const
    {
        if (!device || capacity == 0)
        {
            return false;
        }

        D3D12_DESCRIPTOR_HEAP_DESC heap_desc = {};
        heap_desc.NumDescriptors = capacity;
        heap_desc.Type = heap.heap_type;
        heap_desc.Flags = shader_visible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        if (FAILED(device->CreateDescriptorHeap(&heap_desc, IID_PPV_ARGS(&heap.heap))))
        {
            backlog::Post("Failed to create descriptor heap", backlog::LogLevel::Error);
            return false;
        }

        heap.descriptor_size = device->GetDescriptorHandleIncrementSize(heap.heap_type);
        heap.capacity = capacity;
        heap.allocated_count = 0;
        heap.free_list.clear();
        return true;
    }

    bool DescriptorAllocatorDX12::AllocateFromHeap(DescriptorHeap& heap, uint32& out_descriptor_index)
    {
        if (!heap.heap)
        {
            return false;
        }

        if (!heap.free_list.empty())
        {
            out_descriptor_index = heap.free_list.back();
            heap.free_list.pop_back();
            return true;
        }

        if (heap.allocated_count >= heap.capacity)
        {
            return false;
        }

        out_descriptor_index = heap.allocated_count;
        ++heap.allocated_count;
        return true;
    }

    void DescriptorAllocatorDX12::FreeToHeap(DescriptorHeap& heap, uint32 descriptor_index)
    {
        if (descriptor_index >= heap.allocated_count)
        {
            return;
        }

        heap.free_list.push_back(descriptor_index);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE DescriptorAllocatorDX12::GetCpuHandle(const DescriptorHeap& heap, uint32 descriptor_index) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {};
        if (!heap.heap)
        {
            return cpu_handle;
        }

        cpu_handle = heap.heap->GetCPUDescriptorHandleForHeapStart();
        cpu_handle.ptr += static_cast<Size>(heap.descriptor_size) * descriptor_index;
        return cpu_handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE DescriptorAllocatorDX12::GetGpuHandle(const DescriptorHeap& heap, uint32 descriptor_index) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
        if (!heap.heap)
        {
            return gpu_handle;
        }

        gpu_handle = heap.heap->GetGPUDescriptorHandleForHeapStart();
        gpu_handle.ptr += static_cast<uint64>(heap.descriptor_size) * descriptor_index;
        return gpu_handle;
    }

    bool DescriptorAllocatorDX12::CreateRenderTargetView(RHIResourceDX12& resource,
        const RHISubresourceDesc& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
    {
        ID3D12Resource* native_resource = resource.GetResource();
        if (!native_resource)
        {
            return false;
        }

        const D3D12_RESOURCE_DESC native_desc = native_resource->GetDesc();
        D3D12_RENDER_TARGET_VIEW_DESC rtv_desc = {};
        rtv_desc.Format = desc.format != RHIFormat::Unknown ? ToDXGIFormat(desc.format) : native_desc.Format;
        const uint32 slice_count = desc.slice_count > 0 ? desc.slice_count : 1;
        if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            if (native_desc.SampleDesc.Count > 1)
            {
                if (native_desc.DepthOrArraySize > 1)
                {
                    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
                    rtv_desc.Texture2DMSArray.FirstArraySlice = desc.first_slice;
                    rtv_desc.Texture2DMSArray.ArraySize = slice_count;
                }
                else
                {
                    rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS;
                }
            }
            else if (native_desc.DepthOrArraySize > 1)
            {
                rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY;
                rtv_desc.Texture2DArray.MipSlice = desc.first_mip;
                rtv_desc.Texture2DArray.FirstArraySlice = desc.first_slice;
                rtv_desc.Texture2DArray.ArraySize = slice_count;
                rtv_desc.Texture2DArray.PlaneSlice = 0;
            }
            else
            {
                rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
                rtv_desc.Texture2D.MipSlice = desc.first_mip;
                rtv_desc.Texture2D.PlaneSlice = 0;
            }
        }
        else if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        {
            rtv_desc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D;
            rtv_desc.Texture3D.MipSlice = desc.first_mip;
            rtv_desc.Texture3D.FirstWSlice = desc.first_slice;
            rtv_desc.Texture3D.WSize = slice_count;
        }
        else
        {
            backlog::Post("Unsupported RTV dimension", backlog::LogLevel::Error);
            return false;
        }

        device->CreateRenderTargetView(native_resource, &rtv_desc, cpu_handle);
        return true;
    }

    bool DescriptorAllocatorDX12::CreateDepthStencilView(RHIResourceDX12& resource,
        const RHISubresourceDesc& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
    {
        ID3D12Resource* native_resource = resource.GetResource();
        if (!native_resource)
        {
            return false;
        }

        const D3D12_RESOURCE_DESC native_desc = native_resource->GetDesc();
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = desc.format != RHIFormat::Unknown ? ToDXGIFormat(desc.format) : native_desc.Format;
        const uint32 slice_count = desc.slice_count > 0 ? desc.slice_count : 1;
        if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            if (native_desc.SampleDesc.Count > 1)
            {
                if (native_desc.DepthOrArraySize > 1)
                {
                    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
                    dsv_desc.Texture2DMSArray.FirstArraySlice = desc.first_slice;
                    dsv_desc.Texture2DMSArray.ArraySize = slice_count;
                }
                else
                {
                    dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS;
                }
            }
            else if (native_desc.DepthOrArraySize > 1)
            {
                dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY;
                dsv_desc.Texture2DArray.MipSlice = desc.first_mip;
                dsv_desc.Texture2DArray.FirstArraySlice = desc.first_slice;
                dsv_desc.Texture2DArray.ArraySize = slice_count;
            }
            else
            {
                dsv_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
                dsv_desc.Texture2D.MipSlice = desc.first_mip;
            }
        }
        else
        {
            backlog::Post("Unsupported DSV dimension", backlog::LogLevel::Error);
            return false;
        }

        device->CreateDepthStencilView(native_resource, &dsv_desc, cpu_handle);
        return true;
    }

    bool DescriptorAllocatorDX12::CreateShaderResourceView(RHIResourceDX12& resource,
        const RHISubresourceDesc& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
    {
        ID3D12Resource* native_resource = resource.GetResource();
        if (!native_resource)
        {
            return false;
        }

        const D3D12_RESOURCE_DESC native_desc = native_resource->GetDesc();
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = desc.format != RHIFormat::Unknown ? ToDXGIFormat(desc.format) : native_desc.Format;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

        if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            if (desc.buffer_stride == 0)
            {
                backlog::Post("Shader resource buffer stride must be greater than zero", backlog::LogLevel::Error);
                return false;
            }

            const Size byte_size = desc.buffer_size > 0 ? desc.buffer_size : static_cast<Size>(native_desc.Width);
            const Size first_element = desc.buffer_offset / desc.buffer_stride;
            const Size element_count = byte_size / desc.buffer_stride;
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
            srv_desc.Format = DXGI_FORMAT_UNKNOWN;
            srv_desc.Buffer.FirstElement = static_cast<UINT64>(first_element);
            srv_desc.Buffer.NumElements = static_cast<UINT>(element_count);
            srv_desc.Buffer.StructureByteStride = static_cast<UINT>(desc.buffer_stride);
            srv_desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
        }
        else if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            if (native_desc.DepthOrArraySize > 1)
            {
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
                srv_desc.Texture2DArray.MostDetailedMip = desc.first_mip;
                srv_desc.Texture2DArray.MipLevels = desc.mip_count;
                srv_desc.Texture2DArray.FirstArraySlice = desc.first_slice;
                srv_desc.Texture2DArray.ArraySize = desc.slice_count;
                srv_desc.Texture2DArray.PlaneSlice = 0;
                srv_desc.Texture2DArray.ResourceMinLODClamp = 0.0f;
            }
            else
            {
                srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srv_desc.Texture2D.MostDetailedMip = desc.first_mip;
                srv_desc.Texture2D.MipLevels = desc.mip_count;
                srv_desc.Texture2D.PlaneSlice = 0;
                srv_desc.Texture2D.ResourceMinLODClamp = 0.0f;
            }
        }
        else
        {
            backlog::Post("Unsupported SRV dimension", backlog::LogLevel::Error);
            return false;
        }

        device->CreateShaderResourceView(native_resource, &srv_desc, cpu_handle);
        return true;
    }

    bool DescriptorAllocatorDX12::CreateUnorderedAccessView(RHIResourceDX12& resource,
        const RHISubresourceDesc& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
    {
        ID3D12Resource* native_resource = resource.GetResource();
        if (!native_resource)
        {
            return false;
        }

        const D3D12_RESOURCE_DESC native_desc = native_resource->GetDesc();
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = desc.format != RHIFormat::Unknown ? ToDXGIFormat(desc.format) : native_desc.Format;
        if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            if (desc.buffer_stride == 0)
            {
                backlog::Post("Unordered access buffer stride must be greater than zero", backlog::LogLevel::Error);
                return false;
            }

            const Size byte_size = desc.buffer_size > 0 ? desc.buffer_size : static_cast<Size>(native_desc.Width);
            const Size first_element = desc.buffer_offset / desc.buffer_stride;
            const Size element_count = byte_size / desc.buffer_stride;
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
            uav_desc.Format = DXGI_FORMAT_UNKNOWN;
            uav_desc.Buffer.FirstElement = static_cast<UINT64>(first_element);
            uav_desc.Buffer.NumElements = static_cast<UINT>(element_count);
            uav_desc.Buffer.StructureByteStride = static_cast<UINT>(desc.buffer_stride);
            uav_desc.Buffer.CounterOffsetInBytes = 0;
            uav_desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
        }
        else if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D)
        {
            if (native_desc.DepthOrArraySize > 1)
            {
                uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY;
                uav_desc.Texture2DArray.MipSlice = desc.first_mip;
                uav_desc.Texture2DArray.FirstArraySlice = desc.first_slice;
                uav_desc.Texture2DArray.ArraySize = desc.slice_count;
                uav_desc.Texture2DArray.PlaneSlice = 0;
            }
            else
            {
                uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
                uav_desc.Texture2D.MipSlice = desc.first_mip;
                uav_desc.Texture2D.PlaneSlice = 0;
            }
        }
        else
        {
            backlog::Post("Unsupported UAV dimension", backlog::LogLevel::Error);
            return false;
        }

        device->CreateUnorderedAccessView(native_resource, nullptr, &uav_desc, cpu_handle);
        return true;
    }

    bool DescriptorAllocatorDX12::CreateConstantBufferView(RHIResourceDX12& resource,
        const RHISubresourceDesc& desc,
        D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle)
    {
        ID3D12Resource* native_resource = resource.GetResource();
        if (!native_resource)
        {
            return false;
        }

        const D3D12_RESOURCE_DESC native_desc = native_resource->GetDesc();
        if (native_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            backlog::Post("Constant buffer view requires buffer resource", backlog::LogLevel::Error);
            return false;
        }

        const Size buffer_size = desc.buffer_size > 0 ? desc.buffer_size : static_cast<Size>(native_desc.Width - desc.buffer_offset);
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbv_desc = {};
        cbv_desc.BufferLocation = native_resource->GetGPUVirtualAddress() + static_cast<UINT64>(desc.buffer_offset);
        cbv_desc.SizeInBytes = AlignConstantBufferSize(static_cast<UINT>(buffer_size));
        device->CreateConstantBufferView(&cbv_desc, cpu_handle);
        return true;
    }
}
