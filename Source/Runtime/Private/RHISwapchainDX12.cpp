#include "RHISwapchainDX12.h"
#include "Backlog.h"
#include "Platform.h"
#include "DescriptorAllocatorDX12.h"
#include "DirectX-Headers/d3d12.h"

namespace won::rendering
{
    RHISwapchainDX12::RHISwapchainDX12(ComPtr<ID3D12Device> device_in, ComPtr<IDXGIFactory6> factory_in,
        std::shared_ptr<RHIContextDX12> graphics_context_in,
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in,
        platform::Window& window)
        : device(std::move(device_in))
        , factory(std::move(factory_in))
        , graphics_context(std::move(graphics_context_in))
        , descriptor_allocator(std::move(descriptor_allocator_in))
    {
        if (!device || !factory || !graphics_context || !graphics_context->GetQueue())
        {
            backlog::Post("RHISwapchainDX12 initialization failed", backlog::LogLevel::Error);
            return;
        }

        HWND hwnd = static_cast<HWND>(window.GetNativeHandle());
        if (!hwnd)
        {
            backlog::Post("Window native handle is invalid", backlog::LogLevel::Error);
            return;
        }

        DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = {};
        swap_chain_desc.BufferCount = back_buffer_count;
        swap_chain_desc.Width = static_cast<UINT>(window.GetWidth());
        swap_chain_desc.Height = static_cast<UINT>(window.GetHeight());
        swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        swap_chain_desc.SampleDesc.Count = 1;

        ComPtr<IDXGISwapChain1> temp_swapchain;
        if (FAILED(factory->CreateSwapChainForHwnd(graphics_context->GetQueue(), hwnd, &swap_chain_desc, nullptr, nullptr, &temp_swapchain)))
        {
            backlog::Post("Failed to create DXGI swapchain", backlog::LogLevel::Error);
            return;
        }

        factory->MakeWindowAssociation(hwnd, DXGI_MWA_NO_ALT_ENTER);

        if (FAILED(temp_swapchain.As(&dxgi_swapchain)))
        {
            backlog::Post("Failed to get IDXGISwapChain3", backlog::LogLevel::Error);
            return;
        }

        back_buffers.resize(back_buffer_count);

        for (uint32 i = 0; i < back_buffer_count; ++i)
        {
            ComPtr<ID3D12Resource> back_buffer;
            if (FAILED(dxgi_swapchain->GetBuffer(i, IID_PPV_ARGS(&back_buffer))))
            {
                backlog::Post("Failed to get swapchain back buffer", backlog::LogLevel::Error);
                back_buffers.clear();
                return;
            }

            RHIResourceDesc desc = {};
            desc.type = RHIResourceType::Texture2D;
            desc.texture_desc.width = static_cast<uint32>(window.GetWidth());
            desc.texture_desc.height = static_cast<uint32>(window.GetHeight());
            desc.texture_desc.format = RHIFormat::R8G8B8A8Unorm;
            desc.texture_desc.bind_flags = RHIBindFlags::RenderTarget;

            auto resource = std::make_shared<RHIResourceDX12>(desc, std::move(back_buffer), nullptr, descriptor_allocator);
            resource->SetCurrentState(D3D12_RESOURCE_STATE_PRESENT);
            back_buffers[i] = resource;
        }
    }

    uint32 RHISwapchainDX12::GetCurrentBackBufferIndex() const
    {
        if (!dxgi_swapchain)
        {
            return 0;
        }
        return dxgi_swapchain->GetCurrentBackBufferIndex();
    }

    uint32 RHISwapchainDX12::GetBackBufferCount() const
    {
        return back_buffer_count;
    }

    std::shared_ptr<RHIResource> RHISwapchainDX12::GetCurrentBackBuffer()
    {
        if (!dxgi_swapchain || back_buffers.empty())
        {
            return nullptr;
        }

        const uint32 index = GetCurrentBackBufferIndex();
        if (index >= back_buffers.size())
        {
            return nullptr;
        }

        return back_buffers[index];
    }

    bool RHISwapchainDX12::Present()
    {
        if (!dxgi_swapchain)
        {
            return false;
        }

        return SUCCEEDED(dxgi_swapchain->Present(1, 0));
    }
}
