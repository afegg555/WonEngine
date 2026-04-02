#include "DescriptorAllocatorDX12.h"

#include "Backlog.h"
#include "MathUtils.h"
#include "RHIFormatDX12.h"
#include "RHIResourceDX12.h"

#include <algorithm>

namespace won::rendering
{
    namespace
    {
        constexpr uint32 kRtvCpuStagingDescriptorCount = 4096;
        constexpr uint32 kDsvCpuStagingDescriptorCount = 2048;
        constexpr uint32 kCbvSrvUavCpuStagingDescriptorCount = 1000000;
        constexpr uint32 kSamplerCpuStagingDescriptorCount = 2048;
        constexpr uint32 kCbvSrvUavGpuDescriptorCount = 1000000;
        constexpr uint32 kSamplerGpuDescriptorCount = 2048;

        UINT AlignConstantBufferSize(UINT size)
        {
            return won::math::align(size, static_cast<UINT>(256u));
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

        rtv_cpu_staging_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        dsv_cpu_staging_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        cbv_srv_uav_cpu_staging_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sampler_cpu_staging_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

        cbv_srv_uav_gpu_heap.gpu_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        sampler_gpu_heap.gpu_heap.heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;

        if (!CreateDescriptorHeap(rtv_cpu_staging_heap, kRtvCpuStagingDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(dsv_cpu_staging_heap, kDsvCpuStagingDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(cbv_srv_uav_cpu_staging_heap, kCbvSrvUavCpuStagingDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(sampler_cpu_staging_heap, kSamplerCpuStagingDescriptorCount, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(cbv_srv_uav_gpu_heap.gpu_heap, kCbvSrvUavGpuDescriptorCount, true))
        {
            return;
        }
        if (!CreateDescriptorHeap(sampler_gpu_heap.gpu_heap, kSamplerGpuDescriptorCount, true))
        {
            return;
        }
    }

    bool DescriptorAllocatorDX12::IsValid() const
    {
        return device &&
            rtv_cpu_staging_heap.heap &&
            dsv_cpu_staging_heap.heap &&
            cbv_srv_uav_cpu_staging_heap.heap &&
            sampler_cpu_staging_heap.heap &&
            cbv_srv_uav_gpu_heap.gpu_heap.heap &&
            sampler_gpu_heap.gpu_heap.heap;
    }

    void DescriptorAllocatorDX12::BeginFrame(uint32 frame_slot)
    {
        current_frame_slot = frame_slot;
    }

    bool DescriptorAllocatorDX12::CreateSamplerDescriptor(const D3D12_SAMPLER_DESC& desc,
        int& out_descriptor_index)
    {
        int descriptor_index = -1;
        if (!AllocateFromHeap(sampler_cpu_staging_heap, descriptor_index))
        {
            backlog::Post("Sampler descriptor heap allocation failed", backlog::LogLevel::Error);
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpu_staging_handle = {};
        if (!GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, false, descriptor_index, cpu_staging_handle))
        {
            FreeToHeap(sampler_cpu_staging_heap, descriptor_index);
            return false;
        }

        device->CreateSampler(&desc, cpu_staging_handle);

        D3D12_CPU_DESCRIPTOR_HANDLE gpu_visible_handle = {};
        if (!GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true, descriptor_index, gpu_visible_handle))
        {
            FreeToHeap(sampler_cpu_staging_heap, descriptor_index);
            return false;
        }

        device->CopyDescriptorsSimple(1, gpu_visible_handle, cpu_staging_handle, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
        out_descriptor_index = descriptor_index;
        return true;
    }

    bool DescriptorAllocatorDX12::CreateSubresourceDescriptor(RHIResourceDX12& resource,
        const RHISubresourceDesc& desc,
        D3D12_DESCRIPTOR_HEAP_TYPE& out_heap_type,
        int& out_descriptor_index)
    {
        if (!resource.GetResource())
        {
            return false;
        }

        DescriptorHeap* target_heap = nullptr;
        switch (desc.type)
        {
        case RHISubresourceType::RenderTarget:
            target_heap = &rtv_cpu_staging_heap;
            break;
        case RHISubresourceType::DepthStencil:
            target_heap = &dsv_cpu_staging_heap;
            break;
        case RHISubresourceType::ConstantBuffer:
        case RHISubresourceType::ShaderResource:
        case RHISubresourceType::UnorderedAccess:
            target_heap = &cbv_srv_uav_cpu_staging_heap;
            break;
        default:
            backlog::Post("Unsupported subresource type", backlog::LogLevel::Error);
            return false;
        }

        int descriptor_index = -1;
        if (!AllocateFromHeap(*target_heap, descriptor_index))
        {
            backlog::Post("Descriptor heap allocation failed", backlog::LogLevel::Error);
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cpu_staging_handle{};
        if (!GetCpuDescriptorHandle(target_heap->heap_type, false, descriptor_index, cpu_staging_handle))
        {
            FreeToHeap(*target_heap, descriptor_index);
            return false;
        }

        bool created = false;
        switch (desc.type)
        {
        case RHISubresourceType::RenderTarget:
            created = CreateRenderTargetView(resource, desc, cpu_staging_handle);
            break;
        case RHISubresourceType::DepthStencil:
            created = CreateDepthStencilView(resource, desc, cpu_staging_handle);
            break;
        case RHISubresourceType::ShaderResource:
            created = CreateShaderResourceView(resource, desc, cpu_staging_handle);
            break;
        case RHISubresourceType::UnorderedAccess:
            created = CreateUnorderedAccessView(resource, desc, cpu_staging_handle);
            break;
        case RHISubresourceType::ConstantBuffer:
            created = CreateConstantBufferView(resource, desc, cpu_staging_handle);
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

        if (target_heap->heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        {
            if (static_cast<uint32>(descriptor_index) >= cbv_srv_uav_gpu_heap.gpu_heap.capacity)
            {
                backlog::Post("CBV_SRV_UAV descriptor index exceeds gpu heap capacity", backlog::LogLevel::Error);
                FreeToHeap(*target_heap, descriptor_index);
                return false;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE gpu_visible_handle = {};
            if (!GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, descriptor_index, gpu_visible_handle))
            {
                FreeToHeap(*target_heap, descriptor_index);
                return false;
            }

            device->CopyDescriptorsSimple(1, gpu_visible_handle, cpu_staging_handle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        }

        out_heap_type = target_heap->heap_type;
        out_descriptor_index = descriptor_index;
        return true;
    }

    bool DescriptorAllocatorDX12::GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        bool shader_visible,
        int descriptor_index,
        D3D12_CPU_DESCRIPTOR_HANDLE& out_handle) const
    {
        const DescriptorHeap* des_heap = GetDescriptorHeap(heap_type, shader_visible);
        if (!des_heap || !des_heap->heap || descriptor_index < 0)
        {
            return false;
        }

        out_handle = des_heap->heap->GetCPUDescriptorHandleForHeapStart();
        out_handle.ptr += static_cast<Size>(des_heap->descriptor_size) * static_cast<Size>(descriptor_index);
        return true;
    }

    bool DescriptorAllocatorDX12::GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, int descriptor_index, D3D12_GPU_DESCRIPTOR_HANDLE& out_handle) const
    {
        const DescriptorHeap* des_heap = GetDescriptorHeap(heap_type, true);
        if (!des_heap || !des_heap->heap || descriptor_index < 0)
        {
            return false;
        }

        out_handle = des_heap->heap->GetGPUDescriptorHandleForHeapStart();
        out_handle.ptr += static_cast<Size>(des_heap->descriptor_size) * static_cast<Size>(descriptor_index);
        return true;
    }

    bool DescriptorAllocatorDX12::GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, ID3D12DescriptorHeap** out_heap) const
    {
        if (!out_heap)
            return false;

        *out_heap = nullptr;
        switch (heap_type)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            *out_heap = cbv_srv_uav_gpu_heap.gpu_heap.heap.Get();
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            *out_heap = sampler_gpu_heap.gpu_heap.heap.Get();
            break;
        default:
            break;
        }

        return *out_heap != nullptr;
    }

    void DescriptorAllocatorDX12::ReleaseDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, int descriptor_index)
    {
        DescriptorHeap* cpu_heap = GetDescriptorHeap(heap_type, false);

        if (cpu_heap && cpu_heap->heap && descriptor_index >= 0 && static_cast<uint32>(descriptor_index) < cpu_heap->allocated_count)
        {
            FreeToHeap(*cpu_heap, descriptor_index);
        }
    }

    DescriptorAllocatorDX12::DescriptorHeap* DescriptorAllocatorDX12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, bool shader_visible)
    {
        switch (heap_type)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: return shader_visible ? nullptr : &rtv_cpu_staging_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: return shader_visible ? nullptr : &dsv_cpu_staging_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return shader_visible ? &cbv_srv_uav_gpu_heap.gpu_heap : &cbv_srv_uav_cpu_staging_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: return shader_visible ? &sampler_gpu_heap.gpu_heap : &sampler_cpu_staging_heap;
        default: return nullptr;
        }
    }

    const DescriptorAllocatorDX12::DescriptorHeap* DescriptorAllocatorDX12::GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, bool shader_visible) const
    {
        switch (heap_type)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_RTV: return shader_visible ? nullptr : &rtv_cpu_staging_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_DSV: return shader_visible ? nullptr : &dsv_cpu_staging_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV: return shader_visible ? &cbv_srv_uav_gpu_heap.gpu_heap : &cbv_srv_uav_cpu_staging_heap;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER: return shader_visible ? &sampler_gpu_heap.gpu_heap : &sampler_cpu_staging_heap;
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
        heap.free_list.clear();
        return true;
    }

    bool DescriptorAllocatorDX12::AllocateFromHeap(DescriptorHeap& heap, int& out_descriptor_index)
    {
        std::lock_guard<std::mutex> lock(heap.mutex);

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

        out_descriptor_index = static_cast<int>(heap.allocated_count);
        ++heap.allocated_count;
        return true;
    }

    void DescriptorAllocatorDX12::FreeToHeap(DescriptorHeap& heap, int descriptor_index)
    {
        std::lock_guard<std::mutex> lock(heap.mutex);

        if (descriptor_index < 0 || static_cast<uint32>(descriptor_index) >= heap.allocated_count)
        {
            return;
        }

        heap.free_list.push_back(descriptor_index);
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
        const RHIResourceDesc& resource_desc = resource.GetDesc();
        const RHIFormat logical_format = desc.format != RHIFormat::Unknown ? desc.format : resource_desc.texture_desc.format;
        D3D12_DEPTH_STENCIL_VIEW_DESC dsv_desc = {};
        dsv_desc.Format = ToDXGIDsvFormat(logical_format);
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
        const RHIResourceDesc& resource_desc = resource.GetDesc();
        const RHIFormat logical_format = desc.format != RHIFormat::Unknown ? desc.format : resource_desc.texture_desc.format;
        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = desc.format != RHIFormat::Unknown || resource_desc.type != RHIResourceType::Buffer ? ToDXGISrvFormat(logical_format) : native_desc.Format;
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
        const RHIResourceDesc& resource_desc = resource.GetDesc();
        const RHIFormat logical_format = desc.format != RHIFormat::Unknown ? desc.format : resource_desc.texture_desc.format;
        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = desc.format != RHIFormat::Unknown || resource_desc.type != RHIResourceType::Buffer ? ToDXGIUavFormat(logical_format) : native_desc.Format;
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
