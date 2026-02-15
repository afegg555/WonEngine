#pragma once
#include "RHISwapchain.h"
#include "RHIResourceDX12.h"
#include "RHIContextDX12.h"
#include "Window.h"

#include <dxgi1_6.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;

namespace won::rendering
{
    class DescriptorAllocatorDX12;

    class RHISwapchainDX12 final : public RHISwapchain
    {
    public:
        RHISwapchainDX12(ComPtr<ID3D12Device> device_in, ComPtr<IDXGIFactory6> factory_in,
            std::shared_ptr<RHIContextDX12> graphics_context_in,
            std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in,
            platform::Window& window);

        uint32 GetCurrentBackBufferIndex() const override;
        uint32 GetBackBufferCount() const override;
        std::shared_ptr<RHIResource> GetCurrentBackBuffer() override;
        bool Present() override;

    private:
        ComPtr<ID3D12Device> device;
        ComPtr<IDXGIFactory6> factory;
        std::shared_ptr<RHIContextDX12> graphics_context;
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator;
        ComPtr<IDXGISwapChain3> dxgi_swapchain;
        uint32 back_buffer_count = 2;
        Vector<std::shared_ptr<RHIResourceDX12>> back_buffers;
    };
}
