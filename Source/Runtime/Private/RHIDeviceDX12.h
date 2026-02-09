#pragma once
#include "RHIDevice.h"
#include "RHIContextDX12.h"
#include "DirectX-Headers/d3d12.h"
#include "D3D12MemoryAllocator/D3D12MemAlloc.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct IDXGIFactory6;
struct IDXGIAdapter1;

namespace won::rendering
{
    class RHIDeviceDX12 final : public RHIDevice
    {
    public:
        explicit RHIDeviceDX12(const RHIDeviceDesc& desc);
        ~RHIDeviceDX12() override;

        std::shared_ptr<RHIFence> CreateFence(uint64 initial_value) override;
        std::shared_ptr<RHICommandAllocator> CreateCommandAllocator(RHIQueueType type) override;
        std::shared_ptr<RHICommandList> CreateCommandList(RHIQueueType type) override;

        std::shared_ptr<RHIResource> CreateBuffer(const RHIBufferDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) override;

        std::shared_ptr<RHIResource> CreateTexture(const RHITextureDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) override;

        std::shared_ptr<RHIPipeline> CreateGraphicsPipeline(
            const RHIGraphicsPipelineDesc& desc) override;

        std::shared_ptr<RHIPipeline> CreateComputePipeline(
            const RHIComputePipelineDesc& desc) override;

        std::shared_ptr<RHISampler> CreateSampler(const RHISamplerDesc& desc) override;

    private:
        RHIDeviceDesc device_desc = {};
        ComPtr<IDXGIFactory6> factory;
        ComPtr<IDXGIAdapter1> adapter;
        ComPtr<ID3D12Device> device;
        std::shared_ptr<RHIContextDX12> graphics_context = {};
        std::shared_ptr<RHIContextDX12> compute_context = {};
        std::shared_ptr<RHIContextDX12> copy_context = {};

        ComPtr<D3D12MA::Allocator> resource_allocator;
    };
}
