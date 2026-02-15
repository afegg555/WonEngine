#pragma once
#include "RHICommandAllocator.h"
#include "RHIPipeline.h"
#include "RHISampler.h"
#include "Types.h"

namespace won::rendering
{
    struct RHIViewport
    {
        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float min_depth = 0.0f;
        float max_depth = 1.0f;
    };

    struct RHIRect
    {
        int32 x = 0;
        int32 y = 0;
        int32 width = 0;
        int32 height = 0;
    };

    struct RHIClearColor
    {
        float r = 0.0f;
        float g = 0.0f;
        float b = 0.0f;
        float a = 1.0f;
    };

    struct RHISubresourceBinding
    {
        RHIResource* resource = nullptr;
        RHISubresourceHandle subresource = {};
    };

    class WONENGINE_API RHICommandList
    {
    public:
        virtual ~RHICommandList() = default;

        virtual RHIQueueType GetType() const = 0;

        virtual void Begin(RHICommandAllocator& allocator) = 0;
        virtual void End() = 0;

        virtual void SetGraphicsPipeline(RHIPipeline& pipeline) = 0;
        virtual void SetComputePipeline(RHIPipeline& pipeline) = 0;

        virtual void SetViewport(const RHIViewport& viewport) = 0;
        virtual void SetScissor(const RHIRect& scissor) = 0;

        virtual void SetRenderTargets(const Vector<RHISubresourceBinding>& color_targets,
            const RHISubresourceBinding* depth_target) = 0;

        virtual void ClearRenderTarget(const RHISubresourceBinding& target,
            const RHIClearColor& color) = 0;

        virtual void ClearDepthStencil(const RHISubresourceBinding& target,
            float depth, uint8 stencil) = 0;

        virtual void SetVertexBuffer(RHIResource& resource, Size stride, Size offset = 0, Size size = 0) = 0;
        virtual void SetIndexBuffer(RHIResource& resource, bool index32, Size offset = 0, Size size = 0) = 0;
        virtual void SetPrimitiveTopology(RHIPrimitiveTopology topology) = 0;

        virtual void SetConstantBuffer(RHIShaderStage stage, uint32 slot,
            const RHISubresourceBinding& view) = 0;

        virtual void SetShaderResource(RHIShaderStage stage, uint32 slot,
            const RHISubresourceBinding& view) = 0;

        virtual void SetUnorderedAccess(RHIShaderStage stage, uint32 slot,
            const RHISubresourceBinding& view) = 0;

        virtual void SetSampler(RHIShaderStage stage, uint32 slot,
            const RHISampler& sampler) = 0;

        virtual void PushConstants(RHIShaderStage stage, const void* data,
            Size size, uint32 offset) = 0;

        virtual void Draw(uint32 vertex_count, uint32 instance_count,
            uint32 first_vertex, uint32 first_instance) = 0;

        virtual void DrawIndexed(uint32 index_count, uint32 instance_count,
            uint32 first_index, int32 vertex_offset, uint32 first_instance) = 0;

        virtual void Dispatch(uint32 group_x, uint32 group_y, uint32 group_z) = 0;

        virtual void CopyResource(RHIResource& dest, RHIResource& src) = 0;

        virtual void TransitionResource(RHIResource& resource,
            RHIResourceState after_state) = 0;
    };
}
