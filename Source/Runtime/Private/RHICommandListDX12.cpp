#include "RHICommandListDX12.h"

#include "Backlog.h"
#include "RHICommandAllocatorDX12.h"
#include "RHIPipelineDX12.h"
#include "RHIResourceDX12.h"
#include "DescriptorAllocatorDX12.h"

#include "DirectX-Headers/d3d12.h"

namespace won::rendering
{
    static D3D12_RESOURCE_STATES ToD3D12State(RHIResourceState state)
    {
        switch (state)
        {
        case RHIResourceState::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RHIResourceState::CopyDest: return D3D12_RESOURCE_STATE_COPY_DEST;
        case RHIResourceState::ShaderRead: return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RHIResourceState::ShaderWrite: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RHIResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RHIResourceState::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RHIResourceState::Present: return D3D12_RESOURCE_STATE_PRESENT;
        default: return D3D12_RESOURCE_STATE_COMMON;
        }
    }

    RHICommandListDX12::RHICommandListDX12(RHIQueueType type, ComPtr<ID3D12Device> device_in,
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in)
        : queue_type(type)
        , device(std::move(device_in))
        , descriptor_allocator(std::move(descriptor_allocator_in))
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

        if (descriptor_allocator && queue_type != RHIQueueType::Copy)
        {
            ID3D12DescriptorHeap* heaps[2] = {};
            uint32 heap_count = 0;
            ID3D12DescriptorHeap* frame_resource_heap = descriptor_allocator->GetFrameCbvSrvUavHeap();
            if (frame_resource_heap)
            {
                heaps[heap_count] = frame_resource_heap;
                ++heap_count;
            }

            ID3D12DescriptorHeap* frame_sampler_heap = descriptor_allocator->GetFrameSamplerHeap();
            if (frame_sampler_heap)
            {
                heaps[heap_count] = frame_sampler_heap;
                ++heap_count;
            }

            if (heap_count > 0)
            {
                command_list->SetDescriptorHeaps(heap_count, heaps);
            }
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
        auto* dx12_pipeline = dynamic_cast<RHIPipelineDX12*>(&pipeline);
        if (!command_list || !dx12_pipeline)
        {
            return;
        }

        command_list->SetPipelineState(dx12_pipeline->GetPipelineState());
        command_list->SetGraphicsRootSignature(dx12_pipeline->GetRootSignature());
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

    void RHICommandListDX12::SetRenderTargets(const Vector<RHISubresourceBinding>& render_targets,
        const RHISubresourceBinding* depth_target)
    {
        if (!command_list || !descriptor_allocator)
        {
            return;
        }

        Vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
        rtv_handles.reserve(render_targets.size());
        for (const auto& target : render_targets)
        {
            if (!target.resource)
            {
                continue;
            }

            auto* resource_dx12 = dynamic_cast<RHIResourceDX12*>(target.resource);
            if (!resource_dx12)
            {
                continue;
            }

            D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
            uint32 descriptor_index;
            if (!resource_dx12->GetSubresourceDescriptor(target.subresource,
                    heap_type,
                    descriptor_index))
            {
                continue;
            }

            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
            if (!descriptor_allocator->GetCpuDescriptorHandle(heap_type, descriptor_index, rtv_handle))
            {
                continue;
            }

            rtv_handles.push_back(rtv_handle);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
        D3D12_CPU_DESCRIPTOR_HANDLE* dsv_handle_ptr = nullptr;
        if (depth_target && depth_target->resource)
        {
            auto* depth_resource_dx12 = dynamic_cast<RHIResourceDX12*>(depth_target->resource);
            if (depth_resource_dx12)
            {
                D3D12_DESCRIPTOR_HEAP_TYPE depth_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
                uint32 depth_descriptor_index;
                if (depth_resource_dx12->GetSubresourceDescriptor(depth_target->subresource,
                        depth_heap_type,
                        depth_descriptor_index))
                {
                    if (descriptor_allocator->GetCpuDescriptorHandle(depth_heap_type, depth_descriptor_index, dsv_handle))
                    {
                        dsv_handle_ptr = &dsv_handle;
                    }
                }
            }
        }

        if (rtv_handles.empty() && !dsv_handle_ptr)
        {
            return;
        }

        command_list->OMSetRenderTargets(static_cast<UINT>(rtv_handles.size()),
            rtv_handles.empty() ? nullptr : rtv_handles.data(),
            FALSE,
            dsv_handle_ptr);
    }

    void RHICommandListDX12::ClearRenderTarget(const RHISubresourceBinding& target,
        const RHIClearColor& color)
    {
        if (!command_list || !descriptor_allocator || !target.resource)
        {
            return;
        }

        auto* resource_dx12 = dynamic_cast<RHIResourceDX12*>(target.resource);
        if (!resource_dx12)
        {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        uint32 descriptor_index;
        if (!resource_dx12->GetSubresourceDescriptor(target.subresource,
                heap_type,
                descriptor_index))
        {
            return;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
        if (!descriptor_allocator->GetCpuDescriptorHandle(heap_type, descriptor_index, rtv_handle))
        {
            return;
        }

        FLOAT clear_color[4] = { color.r, color.g, color.b, color.a };
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
    }

    void RHICommandListDX12::ClearDepthStencil(const RHISubresourceBinding& target,
        float depth, uint8 stencil)
    {
        if (!command_list || !descriptor_allocator || !target.resource)
        {
            return;
        }

        auto* resource_dx12 = dynamic_cast<RHIResourceDX12*>(target.resource);
        if (!resource_dx12)
        {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        uint32 descriptor_index;
        if (!resource_dx12->GetSubresourceDescriptor(target.subresource,
                heap_type,
                descriptor_index))
        {
            return;
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
        if (!descriptor_allocator->GetCpuDescriptorHandle(heap_type, descriptor_index, dsv_handle))
        {
            return;
        }

        command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil, 0, nullptr);
    }

    void RHICommandListDX12::SetVertexBuffer(const RHISubresourceBinding& view)
    {
        (void)view;
    }

    void RHICommandListDX12::SetIndexBuffer(const RHISubresourceBinding& view, bool index32)
    {
        (void)view;
        (void)index32;
    }

    void RHICommandListDX12::SetConstantBuffer(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
    {
        (void)stage;
        (void)slot;
        (void)view;
    }

    void RHICommandListDX12::SetShaderResource(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
    {
        (void)stage;
        (void)slot;
        (void)view;
    }

    void RHICommandListDX12::SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
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
        if (!command_list)
        {
            return;
        }

        auto* resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return;
        }

        const D3D12_RESOURCE_STATES before = resource_dx12->GetCurrentState();
        const D3D12_RESOURCE_STATES after = ToD3D12State(after_state);
        if (before == after)
        {
            return;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = resource_dx12->GetResource();
        barrier.Transition.StateBefore = before;
        barrier.Transition.StateAfter = after;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        command_list->ResourceBarrier(1, &barrier);
        resource_dx12->SetCurrentState(after);
    }

    ID3D12GraphicsCommandList* RHICommandListDX12::GetCommandList() const
    {
        return command_list.Get();
    }
}
