#include "RHICommandListDX12.h"

#include "Backlog.h"
#include "RHICommandAllocatorDX12.h"
#include "RHIPipelineDX12.h"
#include "RHIResourceDX12.h"
#include "RHISamplerDX12.h"
#include "DescriptorAllocatorDX12.h"

#include "DirectX-Headers/d3d12.h"
#include <intrin.h> // _BitScanReverse64

namespace won::rendering
{
    static D3D12_RESOURCE_STATES ToD3D12State(RHIQueueType queue_type, RHIResourceState state)
    {
        switch (state)
        {
        case RHIResourceState::CopySource: return D3D12_RESOURCE_STATE_COPY_SOURCE;
        case RHIResourceState::CopyDest: return D3D12_RESOURCE_STATE_COPY_DEST;
        case RHIResourceState::ShaderRead:
            if (queue_type == RHIQueueType::Compute)
            {
                return D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
            }
            return D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
        case RHIResourceState::ConstantBuffer: return D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
        case RHIResourceState::ShaderWrite: return D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        case RHIResourceState::RenderTarget: return D3D12_RESOURCE_STATE_RENDER_TARGET;
        case RHIResourceState::DepthWrite: return D3D12_RESOURCE_STATE_DEPTH_WRITE;
        case RHIResourceState::Present: return D3D12_RESOURCE_STATE_PRESENT;
        default: return D3D12_RESOURCE_STATE_COMMON;
        }
    }

    static D3D_PRIMITIVE_TOPOLOGY ToD3D12PrimitiveTopology(RHIPrimitiveTopology topology)
    {
        switch (topology)
        {
        case RHIPrimitiveTopology::PointList:
            return D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
        case RHIPrimitiveTopology::LineList:
            return D3D_PRIMITIVE_TOPOLOGY_LINELIST;
        case RHIPrimitiveTopology::LineStrip:
            return D3D_PRIMITIVE_TOPOLOGY_LINESTRIP;
        case RHIPrimitiveTopology::TriangleStrip:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP;
        case RHIPrimitiveTopology::TriangleList:
        default:
            return D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
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
        auto dx12_allocator = dynamic_cast<RHICommandAllocatorDX12*>(&allocator);
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
            ID3D12DescriptorHeap* frame_resource_heap{};
            if(descriptor_allocator->GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &frame_resource_heap))
            {
                heaps[heap_count] = frame_resource_heap;
                ++heap_count;
            }

            ID3D12DescriptorHeap* frame_sampler_heap{};
            if (descriptor_allocator->GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, &frame_sampler_heap))
            {
                heaps[heap_count] = frame_sampler_heap;
                ++heap_count;
            }

            if (heap_count > 0)
            {
                command_list->SetDescriptorHeaps(heap_count, heaps);
            }
        }

        descriptor_binding_table = {};
        active_graphics_binding_table = nullptr;
        active_compute_binding_table = nullptr;
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
        auto dx12_pipeline = dynamic_cast<RHIPipelineDX12*>(&pipeline);
        if (!command_list || !dx12_pipeline)
        {
            return;
        }

        command_list->SetPipelineState(dx12_pipeline->GetPipelineState());
        command_list->SetGraphicsRootSignature(dx12_pipeline->GetRootSignature());
        active_graphics_binding_table = &dx12_pipeline->binding_table;

        const RHIPipelineDX12::RootSignatureBindingTable& binding_table = dx12_pipeline->binding_table;

        for (Size index = 0; index < binding_table.slot_infos.size(); ++index)
        {
            if (binding_table.slot_infos[index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                if (binding_table.slot_infos[index].is_bindless)
                {
                    ID3D12DescriptorHeap* heap = nullptr;
                    if (binding_table.slot_infos[index].is_sampler)
                    {
                        descriptor_allocator->GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, &heap);
                    }
                    else
                    {
                        descriptor_allocator->GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap);
                    }
                    command_list->SetGraphicsRootDescriptorTable((UINT)index, heap->GetGPUDescriptorHandleForHeapStart());
                }
            }
        }        
    }

    void RHICommandListDX12::SetComputePipeline(RHIPipeline& pipeline)
    {
        auto dx12_pipeline = dynamic_cast<RHIPipelineDX12*>(&pipeline);

        command_list->SetPipelineState(dx12_pipeline->GetPipelineState());
        command_list->SetComputeRootSignature(dx12_pipeline->GetRootSignature());
        active_compute_binding_table = &dx12_pipeline->binding_table;

        const RHIPipelineDX12::RootSignatureBindingTable& binding_table = dx12_pipeline->binding_table;
        for (Size index = 0; index < binding_table.slot_infos.size(); ++index)
        {
            if (binding_table.slot_infos[index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
                binding_table.slot_infos[index].is_bindless)
            {
                ID3D12DescriptorHeap* heap = nullptr;
                if (binding_table.slot_infos[index].is_sampler)
                {
                    descriptor_allocator->GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, &heap);
                }
                else
                {
                    descriptor_allocator->GetGPUVisibleHeap(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, &heap);
                }
                command_list->SetComputeRootDescriptorTable((UINT)index, heap->GetGPUDescriptorHandleForHeapStart());
            }
        }
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
        Vector<D3D12_CPU_DESCRIPTOR_HANDLE> rtv_handles;
        rtv_handles.reserve(render_targets.size());
        for (const auto& target : render_targets)
        {
            if (!target.resource)
            {
                continue;
            }

            auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(target.resource);
            if (!resource_dx12)
            {
                continue;
            }

            D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;

            D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
            if (!descriptor_allocator->GetCpuDescriptorHandle(heap_type, false, target.subresource.descriptor_index, rtv_handle))
            {
                continue;
            }

            rtv_handles.push_back(rtv_handle);
        }

        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
        D3D12_CPU_DESCRIPTOR_HANDLE* dsv_handle_ptr = nullptr;
        if (depth_target && depth_target->resource)
        {
            auto depth_resource_dx12 = dynamic_cast<RHIResourceDX12*>(depth_target->resource);
            if (depth_resource_dx12)
            {
                D3D12_DESCRIPTOR_HEAP_TYPE depth_heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;

                if (descriptor_allocator->GetCpuDescriptorHandle(depth_heap_type, false, depth_target->subresource.descriptor_index, dsv_handle))
                {
                    dsv_handle_ptr = &dsv_handle;
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
        auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(target.resource);
        if (!resource_dx12)
        {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        D3D12_CPU_DESCRIPTOR_HANDLE rtv_handle = {};
        if (!descriptor_allocator->GetCpuDescriptorHandle(heap_type, false, target.subresource.descriptor_index, rtv_handle))
        {
            return;
        }

        FLOAT clear_color[4] = { color.r, color.g, color.b, color.a };
        command_list->ClearRenderTargetView(rtv_handle, clear_color, 0, nullptr);
    }

    void RHICommandListDX12::ClearDepthStencil(const RHISubresourceBinding& target,
        float depth, uint8 stencil)
    {
        auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(target.resource);
        if (!resource_dx12)
        {
            return;
        }

        D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
        D3D12_CPU_DESCRIPTOR_HANDLE dsv_handle = {};
        if (!descriptor_allocator->GetCpuDescriptorHandle(heap_type, false, target.subresource.descriptor_index, dsv_handle))
        {
            return;
        }

        command_list->ClearDepthStencilView(dsv_handle, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, depth, stencil, 0, nullptr);
    }

    void RHICommandListDX12::SetVertexBuffer(RHIResource& resource, Size stride, Size offset, Size size)
    {
        if (!command_list)
        {
            return;
        }

        auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return;
        }

        if (resource_dx12->GetDesc().type != RHIResourceType::Buffer)
        {
            backlog::Post("SetVertexBuffer requires buffer resource", backlog::LogLevel::Error);
            return;
        }

        if (stride == 0)
        {
            backlog::Post("SetVertexBuffer requires stride > 0", backlog::LogLevel::Error);
            return;
        }

        const D3D12_RESOURCE_DESC native_desc = resource_dx12->GetResource()->GetDesc();
        if (native_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || offset > static_cast<Size>(native_desc.Width))
        {
            backlog::Post("Invalid vertex buffer resource", backlog::LogLevel::Error);
            return;
        }

        const UINT64 size_in_bytes_u64 = size > 0
            ? static_cast<UINT64>(size)
            : (native_desc.Width - static_cast<UINT64>(offset));
        if (offset + static_cast<Size>(size_in_bytes_u64) > static_cast<Size>(native_desc.Width))
        {
            backlog::Post("Vertex buffer range is out of bounds", backlog::LogLevel::Error);
            return;
        }

        if (size_in_bytes_u64 == 0)
        {
            return;
        }

        D3D12_VERTEX_BUFFER_VIEW vbv = {};
        vbv.BufferLocation = resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<UINT64>(offset);
        vbv.SizeInBytes = static_cast<UINT>(size_in_bytes_u64);
        vbv.StrideInBytes = static_cast<UINT>(stride);
        command_list->IASetVertexBuffers(0, 1, &vbv);
    }

    void RHICommandListDX12::SetIndexBuffer(RHIResource& resource, Size stride, Size offset, Size size)
    {
        if (!command_list)
        {
            return;
        }

        auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return;
        }

        if (resource_dx12->GetDesc().type != RHIResourceType::Buffer)
        {
            backlog::Post("SetIndexBuffer requires buffer resource", backlog::LogLevel::Error);
            return;
        }

        if (stride != 2 && stride != 4)
        {
            backlog::Post("SetIndexBuffer requires stride 2 or 4", backlog::LogLevel::Error);
            return;
        }

        const D3D12_RESOURCE_DESC native_desc = resource_dx12->GetResource()->GetDesc();
        if (native_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || offset > static_cast<Size>(native_desc.Width))
        {
            backlog::Post("Invalid index buffer resource", backlog::LogLevel::Error);
            return;
        }

        const UINT64 size_in_bytes_u64 = size > 0
            ? static_cast<UINT64>(size)
            : (native_desc.Width - static_cast<UINT64>(offset));
        if (offset + static_cast<Size>(size_in_bytes_u64) > static_cast<Size>(native_desc.Width))
        {
            backlog::Post("Index buffer range is out of bounds", backlog::LogLevel::Error);
            return;
        }

        if (size_in_bytes_u64 == 0)
        {
            return;
        }

        D3D12_INDEX_BUFFER_VIEW ibv = {};
        ibv.BufferLocation = resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<UINT64>(offset);
        ibv.SizeInBytes = static_cast<UINT>(size_in_bytes_u64);
        ibv.Format = stride == 4 ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT;
        command_list->IASetIndexBuffer(&ibv);
    }

    void RHICommandListDX12::SetPrimitiveTopology(RHIPrimitiveTopology topology)
    {
        if (!command_list)
        {
            return;
        }

        command_list->IASetPrimitiveTopology(ToD3D12PrimitiveTopology(topology));
    }

    void RHICommandListDX12::SetConstantBuffer(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
    {
        assert(slot < descriptor_binder_cbv_count);

        descriptor_binding_table.cbv[slot] = view;
    }

    void RHICommandListDX12::SetShaderResource(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
    {
        assert(slot < descriptor_binder_srv_count);

        descriptor_binding_table.srv[slot] = view;
    }

    void RHICommandListDX12::SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
    {
        assert(slot < descriptor_binder_uav_count);

        descriptor_binding_table.uav[slot] = view;
    }

    void RHICommandListDX12::SetSampler(RHIShaderStage stage, uint32 slot,
        const RHISampler& sampler)
    {
        assert(slot < descriptor_binder_sampler_count);

        descriptor_binding_table.sam[slot] = const_cast<RHISampler*>(&sampler);
    }

    void RHICommandListDX12::PushConstants(RHIShaderStage stage, const void* data,
        Size size, uint32 offset)
    {
        if (!command_list || !data || size == 0)
        {
            return;
        }

        if ((size % sizeof(uint32)) != 0 || (offset % sizeof(uint32)) != 0)
        {
            backlog::Post("PushConstants requires 32-bit aligned size/offset", backlog::LogLevel::Error);
            return;
        }

        const UINT root_parameter_index = 0; // RootConstants(b999) is the first root parameter in DEFAULT_ROOTSIGNATURE.
        const UINT constant_count = static_cast<UINT>(size / sizeof(uint32));
        const UINT destination_offset = static_cast<UINT>(offset / sizeof(uint32));
        if (stage == RHIShaderStage::Compute)
        {
            command_list->SetComputeRoot32BitConstants(root_parameter_index, constant_count, data, destination_offset);
        }
        else
        {
            command_list->SetGraphicsRoot32BitConstants(root_parameter_index, constant_count, data, destination_offset);
        }
    }

    void RHICommandListDX12::Draw(uint32 vertex_count, uint32 instance_count,
        uint32 first_vertex, uint32 first_instance)
    {
        if (command_list)
        {
            ApplyGraphicsDescriptorBindings();
            command_list->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
        }
    }

    void RHICommandListDX12::DrawIndexed(uint32 index_count, uint32 instance_count,
        uint32 first_index, int32 vertex_offset, uint32 first_instance)
    {
        if (command_list)
        {
            ApplyGraphicsDescriptorBindings();
            command_list->DrawIndexedInstanced(index_count, instance_count, first_index,
                vertex_offset, first_instance);
        }
    }

    void RHICommandListDX12::Dispatch(uint32 group_x, uint32 group_y, uint32 group_z)
    {
        if (command_list)
        {
            ApplyComputeDescriptorBindings();
            command_list->Dispatch(group_x, group_y, group_z);
        }
    }

    void RHICommandListDX12::CopyResource(RHIResource& dest, RHIResource& src)
    {
        if (!command_list)
        {
            return;
        }

        auto dest_dx12 = dynamic_cast<RHIResourceDX12*>(&dest);
        auto src_dx12 = dynamic_cast<RHIResourceDX12*>(&src);
        if (!dest_dx12 || !src_dx12)
        {
            backlog::Post("CopyResource requires DX12 resources", backlog::LogLevel::Error);
            return;
        }

        ID3D12Resource* dest_resource = dest_dx12->GetResource();
        ID3D12Resource* src_resource = src_dx12->GetResource();
        if (!dest_resource || !src_resource)
        {
            backlog::Post("CopyResource requires valid native resources", backlog::LogLevel::Error);
            return;
        }

        const D3D12_RESOURCE_DESC dest_desc = dest_resource->GetDesc();
        const D3D12_RESOURCE_DESC src_desc = src_resource->GetDesc();
        if (dest_desc.Dimension != src_desc.Dimension || dest_desc.Width != src_desc.Width)
        {
            backlog::Post("CopyResource dimension/size mismatch", backlog::LogLevel::Error);
            return;
        }

        command_list->CopyResource(dest_resource, src_resource);
    }

    void RHICommandListDX12::CopyBuffer(RHIResource& dest, Size dest_offset, RHIResource& src, Size src_offset, Size size)
    {
        auto dest_dx12 = dynamic_cast<RHIResourceDX12*>(&dest);
        auto src_dx12 = dynamic_cast<RHIResourceDX12*>(&src);

        ID3D12Resource* dest_resource = dest_dx12->GetResource();
        ID3D12Resource* src_resource = src_dx12->GetResource();

        const D3D12_RESOURCE_DESC dest_desc = dest_resource->GetDesc();
        const D3D12_RESOURCE_DESC src_desc = src_resource->GetDesc();
        if (dest_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER || src_desc.Dimension != D3D12_RESOURCE_DIMENSION_BUFFER)
        {
            return;
        }

        command_list->CopyBufferRegion(dest_resource, static_cast<UINT64>(dest_offset), src_resource, static_cast<UINT64>(src_offset), static_cast<UINT64>(size));
    }

    void RHICommandListDX12::TransitionResource(RHIResource& resource,
        RHIResourceState after_state)
    {
        if (!command_list)
        {
            return;
        }

        auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return;
        }

        const D3D12_RESOURCE_STATES before = resource_dx12->GetCurrentState();
        const D3D12_RESOURCE_STATES after = ToD3D12State(queue_type, after_state);
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

    void RHICommandListDX12::TransitionSubresource(RHIResource& resource,
        RHIResourceState before_state,
        RHIResourceState after_state,
        uint32 first_mip, uint32 mip_count,
        uint32 first_slice, uint32 slice_count)
    {
        if (!command_list)
        {
            return;
        }

        auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return;
        }

        const D3D12_RESOURCE_STATES before = ToD3D12State(queue_type, before_state);
        const D3D12_RESOURCE_STATES after = ToD3D12State(queue_type, after_state);
        if (before == after || mip_count == 0 || slice_count == 0)
        {
            return;
        }

        const RHIResourceDesc& resource_desc = resource_dx12->GetDesc();
        const uint32 mip_levels = resource_desc.texture_desc.mip_levels;
        const uint32 total_slices = resource_desc.type == RHIResourceType::Texture3D ? 1u : resource_desc.texture_desc.array_layers;
        if (mip_levels == 0 || first_mip >= mip_levels || first_slice >= total_slices)
        {
            return;
        }

        Vector<D3D12_RESOURCE_BARRIER> barriers;
        barriers.reserve(static_cast<Size>(mip_count) * static_cast<Size>(slice_count));

        for (uint32 slice_index = 0; slice_index < slice_count; ++slice_index)
        {
            for (uint32 mip_index = 0; mip_index < mip_count; ++mip_index)
            {
                D3D12_RESOURCE_BARRIER barrier = {};
                barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
                barrier.Transition.pResource = resource_dx12->GetResource();
                barrier.Transition.StateBefore = before;
                barrier.Transition.StateAfter = after;
                barrier.Transition.Subresource =
                    (first_mip + mip_index) +
                    ((first_slice + slice_index) * mip_levels);
                barriers.push_back(barrier);
            }
        }

        if (!barriers.empty())
        {
            command_list->ResourceBarrier(static_cast<UINT>(barriers.size()), barriers.data());
        }

        if (first_mip == 0 && mip_count == mip_levels && first_slice == 0 && slice_count == total_slices)
        {
            resource_dx12->SetCurrentState(after);
        }
    }

    void RHICommandListDX12::UAVBarrier(RHIResource& resource)
    {
        if (!command_list)
        {
            return;
        }

        auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return;
        }

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barrier.UAV.pResource = resource_dx12->GetResource();
        command_list->ResourceBarrier(1, &barrier);
    }

    ID3D12GraphicsCommandList* RHICommandListDX12::GetCommandList() const
    {
        return command_list.Get();
    }

    void RHICommandListDX12::ApplyGraphicsDescriptorBindings()
    {
        if (!command_list || !active_graphics_binding_table)
        {
            return;
        }

        for (uint32 slot = 0; slot < descriptor_binder_cbv_count; ++slot)
        {
            const uint8 root_index = active_graphics_binding_table->cbv[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                continue;
            }

            const RHISubresourceBinding& view = descriptor_binding_table.cbv[slot];
            auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(view.resource);

            const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(view.subresource);
            const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);

            command_list->SetGraphicsRootConstantBufferView(root_index, gpu_virtual_address);
        }

        for (uint32 slot = 0; slot < descriptor_binder_srv_count; ++slot)
        {
            const uint8 root_index = active_graphics_binding_table->srv[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                continue;
            }

            const RHISubresourceBinding& view = descriptor_binding_table.srv[slot];
            if (!view.IsValid())
            {
                continue;
            }
            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
            descriptor_allocator->GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, view.subresource.descriptor_index, gpu_handle);

            if (active_graphics_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                command_list->SetGraphicsRootDescriptorTable(root_index, gpu_handle);
            }
            else if (active_graphics_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_SRV)
            {
                auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(view.resource);
                const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(view.subresource);

                const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                    resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);
                command_list->SetGraphicsRootShaderResourceView(root_index, gpu_virtual_address);
            }
        }

        for (uint32 slot = 0; slot < descriptor_binder_uav_count; ++slot)
        {
            const uint8 root_index = active_graphics_binding_table->uav[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                continue;
            }

            const RHISubresourceBinding& view = descriptor_binding_table.uav[slot];
            if (!view.IsValid())
            {
                continue;
            }

            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
            descriptor_allocator->GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, view.subresource.descriptor_index, gpu_handle);

            if (active_graphics_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                command_list->SetGraphicsRootDescriptorTable(root_index, gpu_handle);
            }
            else if (active_graphics_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_UAV)
            {
                auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(view.resource);
                const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(view.subresource);

                const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                    resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);
                command_list->SetGraphicsRootUnorderedAccessView(root_index, gpu_virtual_address);
            }
        }

        for (uint32 slot = 0; slot < descriptor_binder_sampler_count; ++slot)
        {
            const uint8 root_index = active_graphics_binding_table->sam[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                continue;
            }

            const RHISampler* sampler = descriptor_binding_table.sam[slot];
            if (!sampler)
            {
                continue;
            }
            auto sampler_dx12 = dynamic_cast<const RHISamplerDX12*>(sampler);

            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
            descriptor_allocator->GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, sampler_dx12->GetDescriptorIndex(), gpu_handle);

            command_list->SetGraphicsRootDescriptorTable(root_index, gpu_handle);
        }
    }

    void RHICommandListDX12::ApplyComputeDescriptorBindings()
    {
        if (!command_list || !active_compute_binding_table)
        {
            return;
        }

        for (uint32 slot = 0; slot < descriptor_binder_cbv_count; ++slot)
        {
            const uint8 root_index = active_compute_binding_table->cbv[slot];
            const RHISubresourceBinding& view = descriptor_binding_table.cbv[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter || !view.IsValid())
            {
                continue;
            }

            auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(view.resource);

            const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(view.subresource);
            const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);

            command_list->SetComputeRootConstantBufferView(root_index, gpu_virtual_address);
        }

        for (uint32 slot = 0; slot < descriptor_binder_srv_count; ++slot)
        {
            const uint8 root_index = active_compute_binding_table->srv[slot];
            const RHISubresourceBinding& view = descriptor_binding_table.srv[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter || !view.IsValid())
            {
                continue;
            }

            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
            descriptor_allocator->GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, view.subresource.descriptor_index, gpu_handle);

            if (active_compute_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                command_list->SetComputeRootDescriptorTable(root_index, gpu_handle);
            }
            else if (active_compute_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_SRV)
            {
                auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(view.resource);
                const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(view.subresource);

                const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                    resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);
                command_list->SetComputeRootShaderResourceView(root_index, gpu_virtual_address);
            }
        }

        for (uint32 slot = 0; slot < descriptor_binder_uav_count; ++slot)
        {
            const uint8 root_index = active_compute_binding_table->uav[slot];
            const RHISubresourceBinding& view = descriptor_binding_table.uav[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter || !view.IsValid())
            {
                continue;
            }

            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
            descriptor_allocator->GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, view.subresource.descriptor_index, gpu_handle);

            if (active_compute_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                command_list->SetComputeRootDescriptorTable(root_index, gpu_handle);
            }
            else if (active_compute_binding_table->slot_infos[root_index].slot_type == D3D12_ROOT_PARAMETER_TYPE_UAV)
            {
                auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(view.resource);
                const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(view.subresource);

                const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                    resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);
                command_list->SetComputeRootUnorderedAccessView(root_index, gpu_virtual_address);
            }
        }

        for (uint32 slot = 0; slot < descriptor_binder_sampler_count; ++slot)
        {
            const uint8 root_index = active_compute_binding_table->sam[slot];
            const RHISampler* sampler = descriptor_binding_table.sam[slot];
            if (root_index == RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter || !sampler)
            {
                continue;
            }

            auto sampler_dx12 = dynamic_cast<const RHISamplerDX12*>(sampler);

            D3D12_GPU_DESCRIPTOR_HANDLE gpu_handle = {};
            descriptor_allocator->GetGpuDescriptorHandle(D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER, sampler_dx12->GetDescriptorIndex(), gpu_handle);

            command_list->SetComputeRootDescriptorTable(root_index, gpu_handle);
        }

    }
 
}
