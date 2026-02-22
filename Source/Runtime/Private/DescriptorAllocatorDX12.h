#pragma once

#include "Types.h"
#include "RHIResource.h"

#include "DirectX-Headers/d3d12.h"

#include <atomic>
#include <mutex>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct ID3D12DescriptorHeap;
struct ID3D12Fence;

namespace won::rendering
{
    class RHIResourceDX12;

    class DescriptorAllocatorDX12 final
    {
    public:
        explicit DescriptorAllocatorDX12(ComPtr<ID3D12Device> device_in);
        bool IsValid() const;

        void BeginFrame(uint32 frame_slot);

        bool CreateSubresourceDescriptor(RHIResourceDX12& resource,
            const RHISubresourceDesc& desc,
            D3D12_DESCRIPTOR_HEAP_TYPE& out_heap_type,
            int& out_descriptor_index);

        bool GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
            bool shader_visible,
            int descriptor_index,
            D3D12_CPU_DESCRIPTOR_HANDLE& out_handle) const;

        bool GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
            int descriptor_index,
            D3D12_GPU_DESCRIPTOR_HANDLE& out_handle) const;

        bool GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
            ID3D12DescriptorHeap** out_heap) const;

        void ReleaseDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, int descriptor_index);

    private:
        struct DescriptorHeap
        {
            D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
            ComPtr<ID3D12DescriptorHeap> heap;
            uint32 descriptor_size = 0;
            uint32 capacity = 0;
            uint32 allocated_count = 0;
            Vector<int> free_list;
            std::mutex mutex;
        };

        struct GpuDescriptorRingHeap
        {
            // Shader-visible heap used by bindless and transient allocations.
            DescriptorHeap gpu_heap;
            std::atomic<uint64> allocation_offset{ 0 };
            ComPtr<ID3D12Fence> fence;
            uint64 fence_value = 0;
            uint64 cached_completed_value = 0;
            uint32 wrap_reservation = 0;
        };

        DescriptorHeap* GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, bool shader_visible);
        const DescriptorHeap* GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, bool shader_visible) const;

        bool CreateDescriptorHeap(DescriptorHeap& state, uint32 capacity, bool shader_visible) const;
        bool AllocateFromHeap(DescriptorHeap& state, int& out_descriptor_index);
        void FreeToHeap(DescriptorHeap& state, int descriptor_index);

        bool CreateRenderTargetView(RHIResourceDX12& resource,
            const RHISubresourceDesc& desc,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle);

        bool CreateDepthStencilView(RHIResourceDX12& resource,
            const RHISubresourceDesc& desc,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle);

        bool CreateShaderResourceView(RHIResourceDX12& resource,
            const RHISubresourceDesc& desc,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle);

        bool CreateUnorderedAccessView(RHIResourceDX12& resource,
            const RHISubresourceDesc& desc,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle);

        bool CreateConstantBufferView(RHIResourceDX12& resource,
            const RHISubresourceDesc& desc,
            D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle);

        ComPtr<ID3D12Device> device;

        DescriptorHeap rtv_cpu_staging_heap;
        DescriptorHeap dsv_cpu_staging_heap;
        DescriptorHeap cbv_srv_uav_cpu_staging_heap;
        DescriptorHeap sampler_cpu_staging_heap;

        GpuDescriptorRingHeap cbv_srv_uav_gpu_heap;
        GpuDescriptorRingHeap sampler_gpu_heap;

        uint32 current_frame_slot = 0;
    };
}
