#include "RHICommandListDX12.h"

#include "Backlog.h"
#include "RHICommandAllocatorDX12.h"
#include "RHIPipelineDX12.h"
#include "RHIQueryHeapDX12.h"
#include "RHIResourceDX12.h"
#include "RHISamplerDX12.h"
#include "DescriptorAllocatorDX12.h"

#include "DirectX-Headers/d3d12.h"
#include <Windows.h>
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
        case RHIResourceState::DepthRead: return D3D12_RESOURCE_STATE_DEPTH_READ | D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
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
        graphics_dirty_params = 0;
        compute_dirty_params = 0;
    }

    void RHICommandListDX12::End()
    {
        if (command_list)
        {
            command_list->Close();
        }
    }

    static D3D12_QUERY_TYPE ToD3D12QueryType(RHIQueryType type)
    {
        switch (type)
        {
        case RHIQueryType::Occlusion:
            return D3D12_QUERY_TYPE_OCCLUSION;
        case RHIQueryType::Timestamp:
        default:
            return D3D12_QUERY_TYPE_TIMESTAMP;
        }
    }

    void RHICommandListDX12::BeginEvent(const char* name)
    {
        const int wide_length = MultiByteToWideChar(CP_UTF8, 0, name, -1, nullptr, 0);
        if (wide_length <= 1)
        {
            return;
        }

        WString wide_name(static_cast<Size>(wide_length), L'\0');
        if (MultiByteToWideChar(CP_UTF8, 0, name, -1, wide_name.data(), wide_length) <= 0)
        {
            return;
        }

        command_list->BeginEvent(0, wide_name.c_str(), static_cast<UINT>(wide_length * sizeof(wchar_t)));
    }

    void RHICommandListDX12::EndEvent()
    {
        if (!command_list)
        {
            return;
        }

        command_list->EndEvent();
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
        //graphics_dirty_params = dx12_pipeline->binding_table.slot_usage;

        const RHIPipelineDX12::RootSignatureBindingTable& binding_table = dx12_pipeline->binding_table;

        for (Size index = 0; index < binding_table.param_infos.size(); ++index)
        {
            if (binding_table.param_infos[index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE)
            {
                if (binding_table.param_infos[index].is_bindless)
                {
                    ID3D12DescriptorHeap* heap = nullptr;
                    if (binding_table.param_infos[index].is_sampler)
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
        //compute_dirty_params = dx12_pipeline->binding_table.slot_usage;

        const RHIPipelineDX12::RootSignatureBindingTable& binding_table = dx12_pipeline->binding_table;
        for (Size index = 0; index < binding_table.param_infos.size(); ++index)
        {
            if (binding_table.param_infos[index].slot_type == D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE &&
                binding_table.param_infos[index].is_bindless)
            {
                ID3D12DescriptorHeap* heap = nullptr;
                if (binding_table.param_infos[index].is_sampler)
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
        if (stage == RHIShaderStage::Compute)
        {
            assert(active_compute_binding_table);
            const uint8 param_index = active_compute_binding_table->cbv[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                compute_dirty_params |= 1ull << param_index;
            }
        }
        else
        {
            assert(active_graphics_binding_table);
            const uint8 param_index = active_graphics_binding_table->cbv[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                graphics_dirty_params |= 1ull << param_index;
            }
        }
    }

    void RHICommandListDX12::SetShaderResource(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
    {
        assert(slot < descriptor_binder_srv_count);

        descriptor_binding_table.srv[slot] = view;
        if (stage == RHIShaderStage::Compute)
        {
            assert(active_compute_binding_table);
            const uint8 param_index = active_compute_binding_table->srv[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                compute_dirty_params |= 1ull << param_index;
            }
        }
        else
        {
            assert(active_graphics_binding_table);
            const uint8 param_index = active_graphics_binding_table->srv[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                graphics_dirty_params |= 1ull << param_index;
            }
        }
    }

    void RHICommandListDX12::SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
        const RHISubresourceBinding& view)
    {
        assert(slot < descriptor_binder_uav_count);

        descriptor_binding_table.uav[slot] = view;
        if (stage == RHIShaderStage::Compute)
        {
            assert(active_compute_binding_table);
            const uint8 param_index = active_compute_binding_table->uav[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                compute_dirty_params |= 1ull << param_index;
            }
        }
        else
        {
            assert(active_graphics_binding_table);
            const uint8 param_index = active_graphics_binding_table->uav[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                graphics_dirty_params |= 1ull << param_index;
            }
        }
    }

    void RHICommandListDX12::SetSampler(RHIShaderStage stage, uint32 slot,
        const RHISampler& sampler)
    {
        assert(slot < descriptor_binder_sampler_count);

        descriptor_binding_table.sam[slot] = const_cast<RHISampler*>(&sampler);
        if (stage == RHIShaderStage::Compute)
        {
            assert(active_compute_binding_table);
            const uint8 param_index = active_compute_binding_table->sam[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                compute_dirty_params |= 1ull << param_index;
            }
        }
        else
        {
            assert(active_graphics_binding_table);
            const uint8 param_index = active_graphics_binding_table->sam[slot];
            if (param_index != RHIPipelineDX12::RootSignatureBindingTable::invalid_root_parameter)
            {
                graphics_dirty_params |= 1ull << param_index;
            }
        }
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
        if (command_list && active_graphics_binding_table)
        {
            ApplyDescriptorBindings(false, *active_graphics_binding_table);
            command_list->DrawInstanced(vertex_count, instance_count, first_vertex, first_instance);
        }
    }

    void RHICommandListDX12::DrawIndexed(uint32 index_count, uint32 instance_count,
        uint32 first_index, int32 vertex_offset, uint32 first_instance)
    {
        if (command_list && active_graphics_binding_table)
        {
            ApplyDescriptorBindings(false, *active_graphics_binding_table);
            command_list->DrawIndexedInstanced(index_count, instance_count, first_index,
                vertex_offset, first_instance);
        }
    }

    void RHICommandListDX12::Dispatch(uint32 group_x, uint32 group_y, uint32 group_z)
    {
        if (command_list && active_compute_binding_table)
        {
            ApplyDescriptorBindings(true, *active_compute_binding_table);
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

    void RHICommandListDX12::BeginQuery(RHIQueryHeap& heap, uint32 index)
    {
        auto query_heap_dx12 = dynamic_cast<RHIQueryHeapDX12*>(&heap);
        if (!command_list || !query_heap_dx12 || !query_heap_dx12->GetQueryHeap())
        {
            return;
        }

        if (heap.GetDesc().type == RHIQueryType::Timestamp)
        {
            return;
        }

        command_list->BeginQuery(query_heap_dx12->GetQueryHeap(), ToD3D12QueryType(heap.GetDesc().type), index);
    }

    void RHICommandListDX12::EndQuery(RHIQueryHeap& heap, uint32 index)
    {
        auto query_heap_dx12 = dynamic_cast<RHIQueryHeapDX12*>(&heap);
        if (!command_list || !query_heap_dx12 || !query_heap_dx12->GetQueryHeap())
        {
            return;
        }

        command_list->EndQuery(query_heap_dx12->GetQueryHeap(), ToD3D12QueryType(heap.GetDesc().type), index);
    }

    void RHICommandListDX12::ResolveQuery(RHIQueryHeap& heap, uint32 start_index, uint32 count, RHIResource& dest_buffer, Size dest_offset)
    {
        auto query_heap_dx12 = dynamic_cast<RHIQueryHeapDX12*>(&heap);
        auto dest_buffer_dx12 = dynamic_cast<RHIResourceDX12*>(&dest_buffer);
        if (!command_list || !query_heap_dx12 || !query_heap_dx12->GetQueryHeap() || !dest_buffer_dx12 || !dest_buffer_dx12->GetResource() || count == 0)
        {
            return;
        }

        command_list->ResolveQueryData(
            query_heap_dx12->GetQueryHeap(),
            ToD3D12QueryType(heap.GetDesc().type),
            start_index,
            count,
            dest_buffer_dx12->GetResource(),
            static_cast<UINT64>(dest_offset)
        );
    }

    void RHICommandListDX12::ResetQuery(RHIQueryHeap& heap, uint32 start_index, uint32 count)
    {
        // TODO
        return;
    }

    void RHICommandListDX12::AliasingBarrier(RHIResource* before, RHIResource& after)
    {
        if (!command_list)
        {
            return;
        }

        auto after_dx12 = dynamic_cast<RHIResourceDX12*>(&after);
        if (!after_dx12 || !after_dx12->GetResource())
        {
            return;
        }

        auto before_dx12 = before ? dynamic_cast<RHIResourceDX12*>(before) : nullptr;

        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_ALIASING;
        barrier.Aliasing.pResourceBefore = before_dx12 ? before_dx12->GetResource() : nullptr;
        barrier.Aliasing.pResourceAfter = after_dx12->GetResource();
        command_list->ResourceBarrier(1, &barrier);
    }

    void RHICommandListDX12::TransitionResource(RHIResource& resource,
        RHIResourceState before_state,
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

        const D3D12_RESOURCE_STATES before = ToD3D12State(queue_type, before_state);
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

    void RHICommandListDX12::ApplyDescriptorBindings(bool compute, const RHIPipelineDX12::RootSignatureBindingTable& binding_table)
    {
        uint64& dirty_params = compute ? compute_dirty_params : graphics_dirty_params;
        if (!command_list || !descriptor_allocator || dirty_params == 0)
        {
            return;
        }

        for (uint32 root_index = 0; root_index < binding_table.param_infos.size(); ++root_index)
        {
            const auto& param_info = binding_table.param_infos[root_index];
            if ((dirty_params & (1ull << root_index)) == 0)
            {
                continue;
            }

            switch (param_info.slot_type)
            {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            {
                if (param_info.is_bindless || param_info.table_descriptor_count == 0)
                {
                    break;
                }

                const D3D12_DESCRIPTOR_HEAP_TYPE heap_type = param_info.is_sampler ? D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER : D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                D3D12_GPU_DESCRIPTOR_HANDLE table_gpu_handle = {};
                if (param_info.descriptor_ranges.size() == 1 && param_info.table_descriptor_count == 1)
                {
                    // fast path
                    const auto& range = param_info.descriptor_ranges[0];
                    if (range.descriptor_count == 1 && range.table_offset == 0)
                    {
                        int descriptor_index = -1;
                        switch (range.range_type)
                        {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                            if (range.base_register < descriptor_binder_cbv_count && descriptor_binding_table.cbv[range.base_register].IsValid())
                            {
                                descriptor_index = descriptor_binding_table.cbv[range.base_register].subresource.descriptor_index;
                            }
                            break;
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                            if (range.base_register < descriptor_binder_srv_count && descriptor_binding_table.srv[range.base_register].IsValid())
                            {
                                descriptor_index = descriptor_binding_table.srv[range.base_register].subresource.descriptor_index;
                            }
                            break;
                        case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                            if (range.base_register < descriptor_binder_uav_count && descriptor_binding_table.uav[range.base_register].IsValid())
                            {
                                descriptor_index = descriptor_binding_table.uav[range.base_register].subresource.descriptor_index;
                            }
                            break;
                        case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                            if (range.base_register < descriptor_binder_sampler_count && descriptor_binding_table.sam[range.base_register])
                            {
                                auto sampler_dx12 = dynamic_cast<const RHISamplerDX12*>(descriptor_binding_table.sam[range.base_register]);
                                descriptor_index = sampler_dx12 ? sampler_dx12->GetDescriptorIndex() : -1;
                            }
                            break;
                        default:
                            break;
                        }
                        if (descriptor_index >= 0)
                        {
                            descriptor_allocator->GetGpuDescriptorHandle(heap_type, descriptor_index, table_gpu_handle);
                        }
                    }
                }
                else
                {
                    DescriptorAllocatorDX12::DescriptorTableAllocation allocation = {};
                    if (!descriptor_allocator->AllocateTransientDescriptors(heap_type, param_info.table_descriptor_count, allocation))
                    {
                        break;
                    }

                    for (const auto& range : param_info.descriptor_ranges)
                    {
                        for (uint32 descriptor_index = 0; descriptor_index < range.descriptor_count; ++descriptor_index)
                        {
                            const uint32 shader_register = range.base_register + descriptor_index;
                            const uint32 table_offset = range.table_offset + descriptor_index;
                            int source_descriptor_index = -1;
                            switch (range.range_type)
                            {
                            case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                                if (shader_register < descriptor_binder_cbv_count && descriptor_binding_table.cbv[shader_register].IsValid())
                                {
                                    source_descriptor_index = descriptor_binding_table.cbv[shader_register].subresource.descriptor_index;
                                }
                                break;
                            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                                if (shader_register < descriptor_binder_srv_count && descriptor_binding_table.srv[shader_register].IsValid())
                                {
                                    source_descriptor_index = descriptor_binding_table.srv[shader_register].subresource.descriptor_index;
                                }
                                break;
                            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                                if (shader_register < descriptor_binder_uav_count && descriptor_binding_table.uav[shader_register].IsValid())
                                {
                                    source_descriptor_index = descriptor_binding_table.uav[shader_register].subresource.descriptor_index;
                                }
                                break;
                            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                                if (shader_register < descriptor_binder_sampler_count && descriptor_binding_table.sam[shader_register])
                                {
                                    auto sampler_dx12 = dynamic_cast<const RHISamplerDX12*>(descriptor_binding_table.sam[shader_register]);
                                    source_descriptor_index = sampler_dx12 ? sampler_dx12->GetDescriptorIndex() : -1;
                                }
                                break;
                            default:
                                break;
                            }
                            if (source_descriptor_index >= 0)
                            {
                                descriptor_allocator->CopyDescriptorToTransientTable(heap_type, source_descriptor_index, allocation, table_offset);
                            }
                            else
                            {
                                descriptor_allocator->CopyNullDescriptorToTransientTable(heap_type, range.range_type, allocation, table_offset);
                            }
                        }
                    }

                    table_gpu_handle = allocation.gpu_handle;
                }

                if (table_gpu_handle.ptr != 0)
                {
                    if (compute)
                    {
                        command_list->SetComputeRootDescriptorTable(root_index, table_gpu_handle);
                    }
                    else
                    {
                        command_list->SetGraphicsRootDescriptorTable(root_index, table_gpu_handle);
                    }
                }
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            {
                const uint32 slot = param_info.shader_register;
                if (param_info.register_space != 0 || slot >= descriptor_binder_cbv_count || !descriptor_binding_table.cbv[slot].IsValid())
                {
                    break;
                }

                auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(descriptor_binding_table.cbv[slot].resource);
                if (!resource_dx12)
                {
                    break;
                }

                const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(descriptor_binding_table.cbv[slot].subresource);
                if (!subresource_entry)
                {
                    break;
                }

                const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                    resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);
                if (compute)
                {
                    command_list->SetComputeRootConstantBufferView(root_index, gpu_virtual_address);
                }
                else
                {
                    command_list->SetGraphicsRootConstantBufferView(root_index, gpu_virtual_address);
                }
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            {
                const uint32 slot = param_info.shader_register;
                if (param_info.register_space != 0 || slot >= descriptor_binder_srv_count || !descriptor_binding_table.srv[slot].IsValid())
                {
                    break;
                }

                auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(descriptor_binding_table.srv[slot].resource);
                if (!resource_dx12)
                {
                    break;
                }

                const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(descriptor_binding_table.srv[slot].subresource);
                if (!subresource_entry)
                {
                    break;
                }

                const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                    resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);
                if (compute)
                {
                    command_list->SetComputeRootShaderResourceView(root_index, gpu_virtual_address);
                }
                else
                {
                    command_list->SetGraphicsRootShaderResourceView(root_index, gpu_virtual_address);
                }
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
            {
                const uint32 slot = param_info.shader_register;
                if (param_info.register_space != 0 || slot >= descriptor_binder_uav_count || !descriptor_binding_table.uav[slot].IsValid())
                {
                    break;
                }

                auto resource_dx12 = dynamic_cast<RHIResourceDX12*>(descriptor_binding_table.uav[slot].resource);
                if (!resource_dx12)
                {
                    break;
                }

                const RHIResourceDX12::SubresourceEntry* subresource_entry = resource_dx12->FindSubresourceEntry(descriptor_binding_table.uav[slot].subresource);
                if (!subresource_entry)
                {
                    break;
                }

                const D3D12_GPU_VIRTUAL_ADDRESS gpu_virtual_address =
                    resource_dx12->GetResource()->GetGPUVirtualAddress() + static_cast<D3D12_GPU_VIRTUAL_ADDRESS>(subresource_entry->desc.buffer_offset);
                if (compute)
                {
                    command_list->SetComputeRootUnorderedAccessView(root_index, gpu_virtual_address);
                }
                else
                {
                    command_list->SetGraphicsRootUnorderedAccessView(root_index, gpu_virtual_address);
                }
                break;
            }
            default:
                break;
            }
        }

        dirty_params = 0;
    }
 
}
