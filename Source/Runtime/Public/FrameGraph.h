#pragma once
#include "JobSystem.h"
#include "FrameContext.h"
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
        bool is_output = false; // something outside the graph consumes it
    };

    class FrameGraph
    {
    public:
        void Reset(RHIDevice& device, FrameContext& frame_context);

        FrameGraphResourceId Import(RHIResource& resource);
		// Passes are executed in the order they are added
        void AddPass(const char* name, Vector<FrameGraphAccess> accesses, std::function<void(RHICommandList&)> execute);

        // Something outside the graph consumes this resource, so whatever produces it must survive culling.
        void MarkOutput(FrameGraphResourceId resource);

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

        RHIDevice* device = nullptr;
        FrameContext* frame_context = nullptr;

        Vector<FrameGraphResource> resources;
        UnorderedMap<RHIResource*, FrameGraphResourceId> resource_ids;
        Vector<Pass> passes;
    };
}
