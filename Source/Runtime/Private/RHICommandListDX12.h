#pragma once

#include "RHICommandList.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct ID3D12GraphicsCommandList;

namespace won::rendering
{
    class RHICommandListDX12 final : public RHICommandList
    {
    public:
        RHICommandListDX12(RHIQueueType type, ComPtr<ID3D12Device> device_in);

        RHIQueueType GetType() const override;

        void Begin(RHICommandAllocator& allocator) override;
        void End() override;

        void SetGraphicsPipeline(RHIPipeline& pipeline) override;
        void SetComputePipeline(RHIPipeline& pipeline) override;

        void SetViewport(const RHIViewport& viewport) override;
        void SetScissor(const RHIRect& scissor) override;

        void SetRenderTargets(const Vector<RHITextureView>& color_targets,
            const RHITextureView* depth_target) override;

        void ClearRenderTarget(const RHITextureView& target,
            const RHIClearColor& color) override;

        void ClearDepthStencil(const RHITextureView& target,
            float depth, uint8 stencil) override;

        void SetVertexBuffer(const RHIBufferView& view) override;
        void SetIndexBuffer(const RHIBufferView& view, bool index32) override;

        void SetConstantBuffer(RHIShaderStage stage, uint32 slot,
            const RHIBufferView& view) override;

        void SetShaderResource(RHIShaderStage stage, uint32 slot,
            const RHITextureView& view) override;

        void SetShaderResource(RHIShaderStage stage, uint32 slot,
            const RHIBufferView& view) override;

        void SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
            const RHITextureView& view) override;

        void SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
            const RHIBufferView& view) override;

        void SetSampler(RHIShaderStage stage, uint32 slot,
            const RHISampler& sampler) override;

        void PushConstants(RHIShaderStage stage, const void* data,
            Size size, uint32 offset) override;

        void Draw(uint32 vertex_count, uint32 instance_count,
            uint32 first_vertex, uint32 first_instance) override;

        void DrawIndexed(uint32 index_count, uint32 instance_count,
            uint32 first_index, int32 vertex_offset, uint32 first_instance) override;

        void Dispatch(uint32 group_x, uint32 group_y, uint32 group_z) override;

        void CopyResource(RHIResource& dest, RHIResource& src) override;

        void TransitionResource(RHIResource& resource,
            RHIResourceState after_state) override;

        ID3D12GraphicsCommandList* GetCommandList() const;

    private:
        RHIQueueType queue_type = RHIQueueType::Graphics;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12GraphicsCommandList> command_list;
    };
}
