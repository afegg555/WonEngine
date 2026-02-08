#include "RHICommandListDX12.h"

#include "Backlog.h"
#include "RHICommandAllocatorDX12.h"

#include "DirectX-Headers/d3d12.h"

namespace won::rendering
{
    RHICommandListDX12::RHICommandListDX12(RHIQueueType type, ComPtr<ID3D12Device> device_in)
        : queue_type(type)
        , device(std::move(device_in))
    {
    }

    RHIQueueType RHICommandListDX12::GetType() const
    {
        return queue_type;
    }

    void RHICommandListDX12::Begin(RHICommandAllocator& allocator)
    {
        auto* dx12_allocator = dynamic_cast<RHICommandAllocatorDX12*>(&allocator);
        if (!dx12_allocator)
        {
            backlog::Post("Invalid command allocator type", backlog::LogLevel::Error);
            return;
        }

        ID3D12CommandAllocator* native_allocator = dx12_allocator->GetAllocator();
        if (!native_allocator)
        {
            backlog::Post("DX12 command allocator is null", backlog::LogLevel::Error);
            return;
        }

        D3D12_COMMAND_LIST_TYPE list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        switch (queue_type)
        {
        case RHIQueueType::Graphics:
            list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            break;
        case RHIQueueType::Compute:
            list_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            break;
        case RHIQueueType::Copy:
            list_type = D3D12_COMMAND_LIST_TYPE_COPY;
            break;
        }

        if (!command_list)
        {
            if (FAILED(device->CreateCommandList(0, list_type, native_allocator, nullptr,
                    IID_PPV_ARGS(&command_list))))
            {
                backlog::Post("Failed to create command list", backlog::LogLevel::Error);
                command_list.Reset();
                return;
            }

            command_list->Close();
        }

        if (FAILED(command_list->Reset(native_allocator, nullptr)))
        {
            backlog::Post("Failed to reset command list", backlog::LogLevel::Error);
            return;
        }
    }

    void RHICommandListDX12::End()
    {
        if (command_list)
        {
            command_list->Close();
        }
    }

    void RHICommandListDX12::SetGraphicsPipeline(RHIPipeline& pipeline)
    {
        (void)pipeline;
    }

    void RHICommandListDX12::SetComputePipeline(RHIPipeline& pipeline)
    {
        (void)pipeline;
    }

    void RHICommandListDX12::SetViewport(const RHIViewport& viewport)
    {
        if (!command_list)
        {
            return;
        }

        D3D12_VIEWPORT vp = {};
        vp.TopLeftX = viewport.x;
        vp.TopLeftY = viewport.y;
        vp.Width = viewport.width;
        vp.Height = viewport.height;
        vp.MinDepth = viewport.min_depth;
        vp.MaxDepth = viewport.max_depth;
        command_list->RSSetViewports(1, &vp);
    }

    void RHICommandListDX12::SetScissor(const RHIRect& scissor)
    {
        if (!command_list)
        {
            return;
        }

        D3D12_RECT rect = {};
        rect.left = scissor.x;
        rect.top = scissor.y;
        rect.right = scissor.x + scissor.width;
        rect.bottom = scissor.y + scissor.height;
        command_list->RSSetScissorRects(1, &rect);
    }

    void RHICommandListDX12::SetRenderTargets(const Vector<RHITextureView>& color_targets,
        const RHITextureView* depth_target)
    {
        (void)color_targets;
        (void)depth_target;
    }

    void RHICommandListDX12::ClearRenderTarget(const RHITextureView& target,
        const RHIClearColor& color)
    {
        (void)target;
        (void)color;
    }

    void RHICommandListDX12::ClearDepthStencil(const RHITextureView& target,
        float depth, uint8 stencil)
    {
        (void)target;
        (void)depth;
        (void)stencil;
    }

    void RHICommandListDX12::SetVertexBuffer(const RHIBufferView& view)
    {
        (void)view;
    }

    void RHICommandListDX12::SetIndexBuffer(const RHIBufferView& view, bool index32)
    {
        (void)view;
        (void)index32;
    }

    void RHICommandListDX12::SetConstantBuffer(RHIShaderStage stage, uint32 slot,
        const RHIBufferView& view)
    {
        (void)stage;
        (void)slot;
        (void)view;
    }

    void RHICommandListDX12::SetShaderResource(RHIShaderStage stage, uint32 slot,
        const RHITextureView& view)
    {
        (void)stage;
        (void)slot;
        (void)view;
    }

    void RHICommandListDX12::SetShaderResource(RHIShaderStage stage, uint32 slot,
        const RHIBufferView& view)
    {
        (void)stage;
        (void)slot;
        (void)view;
    }

    void RHICommandListDX12::SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
        const RHITextureView& view)
    {
        (void)stage;
        (void)slot;
        (void)view;
    }

    void RHICommandListDX12::SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
        const RHIBufferView& view)
    {
        (void)stage;
        (void)slot;
        (void)view;
    }

    void RHICommandListDX12::SetSampler(RHIShaderStage stage, uint32 slot,
        const RHISampler& sampler)
    {
        (void)stage;
        (void)slot;
        (void)sampler;
    }

    void RHICommandListDX12::PushConstants(RHIShaderStage stage, const void* data,
        Size size, uint32 offset)
    {
        (void)stage;
        (void)data;
        (void)size;
        (void)offset;
    }

    void RHICommandListDX12::Draw(uint32 vertex_count, uint32 instance_count,
        uint32 first_vertex, uint32 first_instance)
    {
        if (command_list)
        {
            command_list->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
        }
    }

    void RHICommandListDX12::DrawIndexed(uint32 index_count, uint32 instance_count,
        uint32 first_index, int32 vertex_offset, uint32 first_instance)
    {
        if (command_list)
        {
            command_list->DrawIndexedInstanced(index_count, instance_count, first_index,
                vertex_offset, first_instance);
        }
    }

    void RHICommandListDX12::Dispatch(uint32 group_x, uint32 group_y, uint32 group_z)
    {
        if (command_list)
        {
            command_list->Dispatch(group_x, group_y, group_z);
        }
    }

    void RHICommandListDX12::CopyResource(RHIResource& dest, RHIResource& src)
    {
        (void)dest;
        (void)src;
    }

    void RHICommandListDX12::TransitionResource(RHIResource& resource,
        RHIResourceState after_state)
    {
        (void)resource;
        (void)after_state;
    }

    ID3D12GraphicsCommandList* RHICommandListDX12::GetCommandList() const
    {
        return command_list.Get();
    }
}
