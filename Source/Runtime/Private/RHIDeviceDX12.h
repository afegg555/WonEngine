#pragma once
#include "RHIDevice.h"
#include "RHIContextDX12.h"
#include "RHIQueryHeap.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "DirectX-Headers/d3d12.h"
#include <dxgi1_6.h>
#define D3D12MA_D3D12_HEADERS_ALREADY_INCLUDED
#include "D3D12MemoryAllocator/D3D12MemAlloc.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct IDXGIFactory6;
struct IDXGIAdapter1;

namespace won::rendering
{
    class DescriptorAllocatorDX12;

    class RHIDeviceDX12 final : public RHIDevice
    {
    public:
        explicit RHIDeviceDX12(const RHIDeviceDesc& desc);
        ~RHIDeviceDX12() override;

        void BeginFrame(uint32 frame_slot) override;
        uint32 GetFeatureFlags() const override;
        bool HasFeature(RHIDeviceFeature feature) const override;

        std::unique_ptr<RHIFence> CreateFence(uint64 initial_value) override;
        std::unique_ptr<RHICommandAllocator> CreateCommandAllocator(RHIQueueType type) override;
        std::unique_ptr<RHICommandList> CreateCommandList(RHIQueueType type) override;
        std::unique_ptr<RHIQueryHeap> CreateQueryHeap(const RHIQueryHeapDesc& desc) override;

        std::unique_ptr<RHIResource> CreateBuffer(const RHIBufferDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) override;

        std::unique_ptr<RHIResource> CreateTexture(const RHITextureDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) override;

        Size GetMinOffsetAlignment(const RHIBufferDesc& desc) const override;

        std::unique_ptr<RHIMemoryBlock> AllocateMemory(Size size, Size alignment, RHIMemoryCategory category) override;
        std::unique_ptr<RHIResource> CreatePlacedBuffer(RHIMemoryBlock& block, Size offset, const RHIBufferDesc& desc) override;
        std::unique_ptr<RHIResource> CreatePlacedTexture(RHIMemoryBlock& block, Size offset, const RHITextureDesc& desc) override;
        bool ReplaceResource(RHIResource& resource, RHIMemoryBlock& block, Size offset, std::unique_ptr<RHIResource>& out_retired) override;
        Size GetResourceAllocationSize(const RHIResourceDesc& desc, Size& out_alignment) const override;

        // create persistent subresource
        bool CreateSubresource(RHIResource& resource,
            const RHISubresourceDesc& desc,
            RHISubresourceHandle* out_handle) override;

        std::unique_ptr<RHIPipeline> CreateGraphicsPipeline(
            const RHIGraphicsPipelineDesc& desc) override;

        std::unique_ptr<RHIPipeline> CreateComputePipeline(
            const RHIComputePipelineDesc& desc) override;

        std::unique_ptr<RHISampler> CreateSampler(const RHISamplerDesc& desc) override;
        RHIContext* GetContext(RHIQueueType type) override;
        std::unique_ptr<RHISwapchain> CreateSwapchain(platform::Window& window) override;
        bool GetMemoryUsage(RHIMemoryUsage& out_usage) override;

    private:
        RHIDeviceDesc device_desc = {};
        ComPtr<IDXGIFactory6> factory;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<ID3D12Device> device;
        std::shared_ptr<RHIContextDX12> graphics_context = {};
        std::shared_ptr<RHIContextDX12> compute_context = {};
        std::shared_ptr<RHIContextDX12> copy_context = {};
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator = {};
        uint32 feature_flags = 0;

        ComPtr<D3D12MA::Allocator> resource_allocator;
    };
}
