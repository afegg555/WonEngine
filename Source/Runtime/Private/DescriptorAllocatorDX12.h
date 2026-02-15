#pragma once

#include "Types.h"
#include "RHIResource.h"

#include "DirectX-Headers/d3d12.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct ID3D12DescriptorHeap;

namespace won::rendering
{
    class RHIResourceDX12;

    class DescriptorAllocatorDX12 final
    {
    public:
        explicit DescriptorAllocatorDX12(ComPtr<ID3D12Device> device_in);
        bool IsValid() const;

        void BeginFrame(uint32 frame_index);

        bool CreateSubresourceDescriptor(RHIResourceDX12& resource,
            const RHISubresourceDesc& desc,
            D3D12_DESCRIPTOR_HEAP_TYPE& out_heap_type,
            uint32& out_descriptor_index);

        bool GetCpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
            uint32 descriptor_index,
            D3D12_CPU_DESCRIPTOR_HANDLE& out_handle) const;

        void ReleaseDescriptor(D3D12_DESCRIPTOR_HEAP_TYPE heap_type, uint32 descriptor_index);

        bool CopyToFrameHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type,
            D3D12_CPU_DESCRIPTOR_HANDLE source_handle,
            uint32& out_bindless_index,
            D3D12_GPU_DESCRIPTOR_HANDLE& out_gpu_handle);

        ID3D12DescriptorHeap* GetFrameCbvSrvUavHeap() const;
        ID3D12DescriptorHeap* GetFrameSamplerHeap() const;

    private:
        struct DescriptorHeap
        {
            D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
            ComPtr<ID3D12DescriptorHeap> heap;
            uint32 descriptor_size = 0;
            uint32 capacity = 0;
            uint32 allocated_count = 0;
            Vector<uint32> free_list;
        };

        DescriptorHeap* GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type);
        const DescriptorHeap* GetDescriptorHeap(D3D12_DESCRIPTOR_HEAP_TYPE heap_type) const;

        bool CreateDescriptorHeap(DescriptorHeap& state, uint32 capacity, bool shader_visible) const;
        bool AllocateFromHeap(DescriptorHeap& state, uint32& out_descriptor_index);
        void FreeToHeap(DescriptorHeap& state, uint32 descriptor_index);
        D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(const DescriptorHeap& state, uint32 descriptor_index) const;
        D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(const DescriptorHeap& state, uint32 descriptor_index) const;

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

        DescriptorHeap rtv_master_heap;
        DescriptorHeap dsv_master_heap;
        DescriptorHeap cbv_srv_uav_master_heap;
        DescriptorHeap sampler_master_heap;

        DescriptorHeap cbv_srv_uav_frame_heap;
        DescriptorHeap sampler_frame_heap;
        uint32 frame_cbv_srv_uav_count = 0;
        uint32 frame_sampler_count = 0;
        uint32 current_frame_index = 0;
    };
}
