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
        constexpr uint32 rtv_cpu_staging_descriptor_count = 4096;
        constexpr uint32 dsv_cpu_staging_descriptor_count = 2048;
        constexpr uint32 cbv_srv_uav_cpu_staging_descriptor_count = 1000000;
        constexpr uint32 sampler_cpu_staging_descriptor_count = 2048;
        constexpr uint32 cbv_srv_uav_gpu_descriptor_count = 1000000;
        constexpr uint32 sampler_gpu_descriptor_count = 2048;
        constexpr uint32 cbv_srv_uav_transient_descriptor_count = 262144;
        constexpr uint32 sampler_transient_descriptor_count = 1024;
        constexpr uint32 cbv_srv_uav_persistent_descriptor_count = cbv_srv_uav_gpu_descriptor_count - cbv_srv_uav_transient_descriptor_count;
        constexpr uint32 sampler_persistent_descriptor_count = sampler_gpu_descriptor_count - sampler_transient_descriptor_count;

        UINT AlignConstantBufferSize(UINT size)
        {
            return won::math::Align(size, static_cast<UINT>(256u));
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

        if (!CreateDescriptorHeap(rtv_cpu_staging_heap, rtv_cpu_staging_descriptor_count, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(dsv_cpu_staging_heap, dsv_cpu_staging_descriptor_count, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(cbv_srv_uav_cpu_staging_heap, cbv_srv_uav_cpu_staging_descriptor_count, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(sampler_cpu_staging_heap, sampler_cpu_staging_descriptor_count, false))
        {
            return;
        }
        if (!CreateDescriptorHeap(cbv_srv_uav_gpu_heap.gpu_heap, cbv_srv_uav_gpu_descriptor_count, true))
        {
            return;
        }
        if (!CreateDescriptorHeap(sampler_gpu_heap.gpu_heap, sampler_gpu_descriptor_count, true))
        {
            return;
        }
        if (!CreateNullDescriptors())
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
            sampler_gpu_heap.gpu_heap.heap &&
            null_cbv_descriptor_index >= 0 &&
            null_srv_descriptor_index >= 0 &&
            null_uav_descriptor_index >= 0 &&
            null_sampler_descriptor_index >= 0;
    }

    void DescriptorAllocatorDX12::BeginFrame(uint32 frame_slot)
    {
        current_frame_slot = frame_slot % max_frames_in_flight;
        cbv_srv_uav_gpu_heap.allocation_offsets[current_frame_slot].store(0);
        sampler_gpu_heap.allocation_offsets[current_frame_slot].store(0);
    }

    bool DescriptorAllocatorDX12::CreateSamplerDescriptor(const D3D12_SAMPLER_DESC& desc,
        int& out_descriptor_index)
    {
        int descriptor_index = -1;
        if (!AllocateFromHeap(sampler_cpu_staging_heap, sampler_persistent_descriptor_count, descriptor_index))
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
        const uint32 max_descriptor_count = target_heap->heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV ?
            cbv_srv_uav_persistent_descriptor_count :
            target_heap->capacity;
        if (!AllocateFromHeap(*target_heap, max_descriptor_count, descriptor_index))
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

    bool DescriptorAllocatorDX12::AllocateTransientDescriptors(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        uint32 count,
        DescriptorTableAllocation& out_allocation)
    {
        out_allocation = {};
        if (count == 0)
        {
            return false;
        }

        GpuDescriptorRingHeap* ring_heap = nullptr;
        uint32 transient_start = 0;
        uint32 transient_count = 0;
        switch (heap_type)
        {
        case D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
            ring_heap = &cbv_srv_uav_gpu_heap;
            transient_start = cbv_srv_uav_persistent_descriptor_count;
            transient_count = cbv_srv_uav_transient_descriptor_count;
            break;
        case D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER:
            ring_heap = &sampler_gpu_heap;
            transient_start = sampler_persistent_descriptor_count;
            transient_count = sampler_transient_descriptor_count;
            break;
        default:
            return false;
        }

        const uint32 descriptors_per_frame = transient_count / max_frames_in_flight;
        if (count > descriptors_per_frame)
        {
            backlog::Post("Transient descriptor heap allocation exceeds per-frame capacity", backlog::LogLevel::Error);
            return false;
        }

        uint64 offset = ring_heap->allocation_offsets[current_frame_slot].load();
        while (true)
        {
            if (offset + count > descriptors_per_frame)
            {
                backlog::Post("Transient descriptor heap allocation failed", backlog::LogLevel::Error);
                return false;
            }
            if (ring_heap->allocation_offsets[current_frame_slot].compare_exchange_weak(offset, offset + count))
            {
                break;
            }
        }

        out_allocation.descriptor_index = transient_start + current_frame_slot * descriptors_per_frame + static_cast<uint32>(offset);
        out_allocation.count = count;
        if (!GetCpuDescriptorHandle(heap_type, true, static_cast<int>(out_allocation.descriptor_index), out_allocation.cpu_handle) ||
            !GetGpuDescriptorHandle(heap_type, static_cast<int>(out_allocation.descriptor_index), out_allocation.gpu_handle))
        {
            out_allocation = {};
            return false;
        }

        return true;
    }

    bool DescriptorAllocatorDX12::CopyDescriptorToTransientTable(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        int source_descriptor_index,
        const DescriptorTableAllocation& allocation,
        uint32 table_offset)
    {
        if (!allocation.IsValid() || table_offset >= allocation.count)
        {
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE source_handle = {};
        if (!GetCpuDescriptorHandle(heap_type, false, source_descriptor_index, source_handle))
        {
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE destination_handle = allocation.cpu_handle;
        const DescriptorHeap* heap = GetDescriptorHeap(heap_type, true);
        if (!heap)
        {
            return false;
        }
        destination_handle.ptr += static_cast<Size>(heap->descriptor_size) * static_cast<Size>(table_offset);
        device->CopyDescriptorsSimple(1, destination_handle, source_handle, heap_type);
        return true;
    }

    bool DescriptorAllocatorDX12::CopyNullDescriptorToTransientTable(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
        D3D12_DESCRIPTOR_RANGE_TYPE range_type,
        const DescriptorTableAllocation& allocation,
        uint32 table_offset)
    {
        int descriptor_index = -1;
        if (heap_type == D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER)
        {
            descriptor_index = null_sampler_descriptor_index;
        }
        else
        {
            switch (range_type)
            {
            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                descriptor_index = null_cbv_descriptor_index;
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                descriptor_index = null_srv_descriptor_index;
                break;
            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                descriptor_index = null_uav_descriptor_index;
                break;
            default:
                break;
            }
        }

        return CopyDescriptorToTransientTable(heap_type, descriptor_index, allocation, table_offset);
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
        return AllocateFromHeap(heap, heap.capacity, out_descriptor_index);
    }

    bool DescriptorAllocatorDX12::AllocateFromHeap(DescriptorHeap& heap, uint32 max_count, int& out_descriptor_index)
    {
        std::lock_guard<std::mutex> lock(heap.mutex);

        for (Size free_index = heap.free_list.size(); free_index > 0; --free_index)
        {
            const int descriptor_index = heap.free_list[free_index - 1];
            if (descriptor_index >= 0 && static_cast<uint32>(descriptor_index) < max_count)
            {
                out_descriptor_index = descriptor_index;
                heap.free_list.erase(heap.free_list.begin() + static_cast<ptrdiff_t>(free_index - 1));
                return true;
            }
        }

        if (heap.allocated_count >= heap.capacity || heap.allocated_count >= max_count)
        {
            return false;
        }

        out_descriptor_index = static_cast<int>(heap.allocated_count);
        ++heap.allocated_count;
        return true;
    }

    bool DescriptorAllocatorDX12::CreateNullDescriptors()
    {
        if (!AllocateFromHeap(cbv_srv_uav_cpu_staging_heap, cbv_srv_uav_persistent_descriptor_count, null_cbv_descriptor_index) ||
            !AllocateFromHeap(cbv_srv_uav_cpu_staging_heap, cbv_srv_uav_persistent_descriptor_count, null_srv_descriptor_index) ||
            !AllocateFromHeap(cbv_srv_uav_cpu_staging_heap, cbv_srv_uav_persistent_descriptor_count, null_uav_descriptor_index) ||
            !AllocateFromHeap(sampler_cpu_staging_heap, sampler_persistent_descriptor_count, null_sampler_descriptor_index))
        {
            backlog::Post("Failed to allocate null descriptors", backlog::LogLevel::Error);
            return false;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE null_cbv = {};
        D3D12_CPU_DESCRIPTOR_HANDLE null_srv = {};
        D3D12_CPU_DESCRIPTOR_HANDLE null_uav = {};
        D3D12_CPU_DESCRIPTOR_HANDLE null_sampler = {};
        if (!GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false, null_cbv_descriptor_index, null_cbv) ||
            !GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false, null_srv_descriptor_index, null_srv) ||
            !GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false, null_uav_descriptor_index, null_uav) ||
            !GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, false, null_sampler_descriptor_index, null_sampler))
        {
            return false;
        }

        device->CreateConstantBufferView(nullptr, null_cbv);

        D3D12_SHADER_RESOURCE_VIEW_DESC srv_desc = {};
        srv_desc.Format = DXGI_FORMAT_R32_UINT;
        srv_desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        srv_desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        srv_desc.Buffer.NumElements = 1;
        device->CreateShaderResourceView(nullptr, &srv_desc, null_srv);

        D3D12_UNORDERED_ACCESS_VIEW_DESC uav_desc = {};
        uav_desc.Format = DXGI_FORMAT_R32_UINT;
        uav_desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        uav_desc.Buffer.NumElements = 1;
        device->CreateUnorderedAccessView(nullptr, nullptr, &uav_desc, null_uav);

        D3D12_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
        sampler_desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler_desc.MaxLOD = D3D12_FLOAT32_MAX;
        device->CreateSampler(&sampler_desc, null_sampler);

        D3D12_CPU_DESCRIPTOR_HANDLE gpu_visible_handle = {};
        GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, null_cbv_descriptor_index, gpu_visible_handle);
        device->CopyDescriptorsSimple(1, gpu_visible_handle, null_cbv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, null_srv_descriptor_index, gpu_visible_handle);
        device->CopyDescriptorsSimple(1, gpu_visible_handle, null_srv, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true, null_uav_descriptor_index, gpu_visible_handle);
        device->CopyDescriptorsSimple(1, gpu_visible_handle, null_uav, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, true, null_sampler_descriptor_index, gpu_visible_handle);
        device->CopyDescriptorsSimple(1, gpu_visible_handle, null_sampler, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER);
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
        else if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        {
            srv_desc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
            srv_desc.Texture3D.MostDetailedMip = desc.first_mip;
            srv_desc.Texture3D.MipLevels = desc.mip_count;
            srv_desc.Texture3D.ResourceMinLODClamp = 0.0f;
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
        else if (native_desc.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D)
        {
            const uint32 slice_count = desc.slice_count > 0 ? desc.slice_count : resource_desc.texture_desc.depth;
            uav_desc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D;
            uav_desc.Texture3D.MipSlice = desc.first_mip;
            uav_desc.Texture3D.FirstWSlice = desc.first_slice;
            uav_desc.Texture3D.WSize = slice_count;
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
