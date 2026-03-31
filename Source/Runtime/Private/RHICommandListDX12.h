#pragma once
#include "RHICommandList.h"
#include "RHIPipelineDX12.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace won::rendering
{
    class DescriptorAllocatorDX12;

    class RHICommandListDX12 final : public RHICommandList
    {
    public:
        RHICommandListDX12(RHIQueueType type, ComPtr<ID3D12Device> device_in,
            std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator_in);

        RHIQueueType GetType() const override;

        void Begin(RHICommandAllocator& allocator) override;
        void End() override;
        void BeginEvent(const char* name) override;
        void EndEvent() override;

        void SetGraphicsPipeline(RHIPipeline& pipeline) override;
        void SetComputePipeline(RHIPipeline& pipeline) override;

        void SetViewport(const RHIViewport& viewport) override;
        void SetScissor(const RHIRect& scissor) override;

        void SetRenderTargets(const Vector<RHISubresourceBinding>& color_targets,
            const RHISubresourceBinding* depth_target) override;

        void ClearRenderTarget(const RHISubresourceBinding& target,
            const RHIClearColor& color) override;

        void ClearDepthStencil(const RHISubresourceBinding& target,
            float depth, uint8 stencil) override;

        // this is for the case of using input assembler
        void SetVertexBuffer(RHIResource& resource, Size stride, Size offset = 0, Size size = 0) override;
        void SetIndexBuffer(RHIResource& resource, Size stride, Size offset = 0, Size size = 0) override;
        void SetPrimitiveTopology(RHIPrimitiveTopology topology) override;
        
        //// slot based binding functions
        void SetConstantBuffer(RHIShaderStage stage, uint32 slot,
            const RHISubresourceBinding& view) override;
        void SetShaderResource(RHIShaderStage stage, uint32 slot,
            const RHISubresourceBinding& view) override;
        void SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
            const RHISubresourceBinding& view) override;
        void SetSampler(RHIShaderStage stage, uint32 slot,
            const RHISampler& sampler) override;
        ////////

        void PushConstants(RHIShaderStage stage, const void* data,
            Size size, uint32 offset) override;

        void Draw(uint32 vertex_count, uint32 instance_count,
            uint32 first_vertex, uint32 first_instance) override;

        void DrawIndexed(uint32 index_count, uint32 instance_count,
            uint32 first_index, int32 vertex_offset, uint32 first_instance) override;

        void Dispatch(uint32 group_x, uint32 group_y, uint32 group_z) override;

        void CopyResource(RHIResource& dest, RHIResource& src) override;
        void CopyBuffer(RHIResource& dest, Size dest_offset, RHIResource& src, Size src_offset, Size size) override;

        void TransitionResource(RHIResource& resource,
            RHIResourceState after_state) override;
        void TransitionSubresource(RHIResource& resource,
            RHIResourceState before_state,
            RHIResourceState after_state,
            uint32 first_mip = 0, uint32 mip_count = 1,
            uint32 first_slice = 1, uint32 slice_count = 1) override;
        void UAVBarrier(RHIResource& resource) override;

        ID3D12GraphicsCommandList* GetCommandList() const;

    private:
        void ApplyGraphicsDescriptorBindings();
        void ApplyComputeDescriptorBindings();

        RHIQueueType queue_type = RHIQueueType::Graphics;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12GraphicsCommandList> command_list;
        std::shared_ptr<DescriptorAllocatorDX12> descriptor_allocator = {};
        const RHIPipelineDX12::RootSignatureBindingTable* active_graphics_binding_table = nullptr;
        const RHIPipelineDX12::RootSignatureBindingTable* active_compute_binding_table = nullptr;
    };
}
