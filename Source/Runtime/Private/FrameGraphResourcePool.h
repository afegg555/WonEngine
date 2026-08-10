#pragma once
#include "FrameContext.h"
#include "RHIResource.h"
#include "Types.h"

namespace won::rendering
{
    class RHIDevice;

    class FrameGraphResourcePool
    {
    public:
        void BeginFrame(RHIDevice& device, FrameContext& frame_context);

        RHIResource* CreateBuffer(uint32 scope, const char* name, const RHIBufferDesc& desc);
        RHIResource* CreateTexture(uint32 scope, const char* name, const RHITextureDesc& desc);
        RHISubresourceHandle GetSubresource(RHIResource& resource, const RHISubresourceDesc& desc);

    private:
        struct PooledSubresource
        {
            RHISubresourceDesc desc;
            RHISubresourceHandle handle;
        };

        struct PooledResource
        {
            uint32 scope = 0;
            String name;
            RHIResourceDesc desc;
            std::unique_ptr<RHIResource> resource;
            Vector<PooledSubresource> subresources;
            uint32 unused_frames = 0;
        };

        RHIResource* Create(uint32 scope, const char* name, const RHIResourceDesc& desc);

        RHIDevice* device = nullptr;
        FrameContext* frame_context = nullptr;
        Vector<PooledResource> pool;
    };
}
