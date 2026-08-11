#pragma once
#include "JobSystem.h"
#include "FrameContext.h"
#include "FrameGraphResourcePool.h"
#include "RHIResource.h"
#include "Types.h"

#include <functional>

namespace won::rendering
{
    class RHICommandList;
    class RHIDevice;

    using FrameGraphResourceId = uint32;
    inline constexpr FrameGraphResourceId invalid_frame_graph_resource = ~0u;

    enum class FrameGraphAccessType
    {
        Read,
        Write,
        ReadWrite,
    };

    struct FrameGraphAccess
    {
        FrameGraphResourceId resource = invalid_frame_graph_resource;
        RHIResourceState state = RHIResourceState::ShaderRead;
        FrameGraphAccessType type = FrameGraphAccessType::Read;
    };

    struct FrameGraphResource
    {
        RHIResource* resource = nullptr;
        RHIResourceState state = RHIResourceState::Undefined;
        bool no_cull = false;
        Size first_pass = 0;
        Size last_pass = 0;
        bool alive = false;
        bool transient = false;
    };

    class FrameGraph
    {
    public:
        void Reset(RHIDevice& device, FrameContext& frame_context);

        FrameGraphResourceId Import(RHIResource& resource);

        RHIResource* CreateBuffer(uint32 scope, const char* name, const RHIBufferDesc& desc);
        RHIResource* CreateTexture(uint32 scope, const char* name, const RHITextureDesc& desc);
        RHISubresourceHandle GetSubresource(RHIResource& resource, const RHISubresourceDesc& desc);

        bool QueueBufferUpload(FrameGraphResourceId destination, const void* data, Size size, RHIResourceState final_state, Size destination_offset = 0);
		// Passes are executed in the order they are added
        void AddPass(const char* name, Vector<FrameGraphAccess> accesses, std::function<void(RHICommandList&)> execute);

        // Something outside the graph consumes this resource, so whatever produces it must survive culling.
        void MarkNoCull(FrameGraphResourceId resource);

        void Compile();
        void Dispatch(jobsystem::Context& context);

    private:
        struct Pass
        {
            String name;
            Vector<FrameGraphAccess> accesses;
            std::function<void(RHICommandList&)> execute;
            RHICommandList* command_list = nullptr;
        };

        struct BufferUpload
        {
            RHIResource* destination = nullptr;
            Size destination_offset = 0;
            RHIResource* source = nullptr;
            Size source_offset = 0;
            Size size = 0;
            RHIResourceState final_state = RHIResourceState::ShaderRead;
        };

        RHIDevice* device = nullptr;
        FrameContext* frame_context = nullptr;

        Vector<FrameGraphResource> resources;
        UnorderedMap<RHIResource*, FrameGraphResourceId> resource_ids;
        Vector<Pass> passes;
        FrameGraphResourcePool pool;
        Vector<BufferUpload> buffer_uploads;
        Size upload_pass_index = 0;
        bool has_upload_pass = false;
    };
}
