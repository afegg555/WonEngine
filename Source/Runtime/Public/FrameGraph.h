#pragma once
#include "JobSystem.h"
#include "RHIResource.h"
#include "RHICommandList.h"
#include "Types.h"

namespace won::rendering
{
    class RHICommandList;
    class RHIDevice;
    struct FrameContext;

    using FrameResourceId = uint32;
    inline constexpr FrameResourceId invalid_frame_resource = ~0u;

    struct FrameResourceAccess
    {
        enum class Type
        {
            Read,
            Write,
            ReadWrite,
        };

        FrameResourceId resource = invalid_frame_resource;
        RHIResourceState state = RHIResourceState::ShaderRead;
        Type type = Type::Read;
    };

    struct FrameResource
    {
        RHIResource* resource = nullptr;
        uint32 scope = 0;
        const char* name = nullptr;
        RHIResourceDesc desc;
        RHIResourceState state = RHIResourceState::Undefined;
        RHIResourceState entry_state = RHIResourceState::Undefined;
        bool no_cull = false;
        uint32 first_pass = 0;
        uint32 last_pass = 0;
        bool alive = false;
        bool transient = false;
    };

    class FrameGraph;

    struct FrameGraphPassContext
    {
        RHICommandList* command_list = nullptr;

        RHIResource* GetResource(FrameResourceId resource) const;

    private:
        const FrameResource* frame_resources = nullptr;
        Size frame_resource_count = 0;
        friend class FrameGraph;
    };

    class FrameGraph
    {
    public:
        void Initialize(RHIDevice* device);
        void BeginFrame(FrameContext& frame_context);

        FrameResourceId Import(RHIResource& resource, RHIResourceState state = RHIResourceState::Undefined);
        FrameResourceId CreateBuffer(uint32 scope, const char* name, const RHIBufferDesc& desc);
        FrameResourceId CreateTexture(uint32 scope, const char* name, const RHITextureDesc& desc);
        RHISubresourceHandle CreateSubresource(FrameResourceId resource, const RHISubresourceDesc& desc);

		void MarkNoCull(FrameResourceId resource); // this resource will not be culled even if it is not used in any pass. and this resource is not aliased
        bool QueueBufferUpload(FrameResourceId destination, const void* data, Size size, Size destination_offset = 0);

        template<typename T>
        FrameResourceId CreateConstants(uint32 scope, const char* name, const T& constants, RHISubresourceHandle& out_cbv)
        {
            RHIBufferDesc desc = {};
            desc.size = sizeof(T);
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = RHIBindFlags::ConstantBuffer;

            const FrameResourceId id = CreateBuffer(scope, name, desc);
            if (id == invalid_frame_resource)
            {
                return invalid_frame_resource;
            }

            RHISubresourceDesc cbv_desc = {};
            cbv_desc.type = RHISubresourceType::ConstantBuffer;
            cbv_desc.buffer_offset = 0;
            cbv_desc.buffer_size = sizeof(T);
            out_cbv = CreateSubresource(id, cbv_desc);

            if (!QueueBufferUpload(id, &constants, sizeof(T)))
            {
                return invalid_frame_resource;
            }
            return id;
        }

        template<typename T>
        FrameResourceId CreateStructuredBuffer(uint32 scope, const char* name, const T* elements, Size count, RHISubresourceHandle& out_srv)
        {
            if (count == 0)
            {
                return invalid_frame_resource;
            }

            RHIBufferDesc desc = {};
            desc.size = sizeof(T) * count;
            desc.usage = RHIResourceUsage::Default;
            desc.bind_flags = RHIBindFlags::ShaderResource;

            const FrameResourceId id = CreateBuffer(scope, name, desc);
            if (id == invalid_frame_resource)
            {
                return invalid_frame_resource;
            }

            RHISubresourceDesc srv_desc = {};
            srv_desc.type = RHISubresourceType::ShaderResource;
            srv_desc.buffer_offset = 0;
            srv_desc.buffer_size = desc.size;
            srv_desc.buffer_stride = sizeof(T);
            out_srv = CreateSubresource(id, srv_desc);

            if (!QueueBufferUpload(id, elements, desc.size))
            {
                return invalid_frame_resource;
            }
            return id;
        }

        // Applied to every pass command list opened after this call; passes only override it when they need a sub-rect.
        void SetDefaultViewport(const RHIViewport& viewport, const RHIRect& scissor);

        // Note: currently ShaderFrame should be read in all passes, so we don't add it to accesses.
        void AddPass(const char* name, Vector<FrameResourceAccess> accesses, std::function<void(const FrameGraphPassContext&)> execute);

        void Compile();
        void Dispatch(jobsystem::Context& context);

    private:
        struct Pass
        {
            String name;
            Vector<FrameResourceAccess> accesses;
            std::function<void(const FrameGraphPassContext&)> execute;
            RHICommandList* command_list = nullptr;
            RHIViewport viewport = {};
            RHIRect scissor = {};
            bool alive = false;
        };

        RHIViewport default_viewport = {};
        RHIRect default_scissor = {};

        struct PooledSubresource
        {
            RHISubresourceDesc desc;
            RHISubresourceHandle handle;
            bool realized = false;
        };

        struct PooledResource
        {
            uint32 scope = 0;
            String name;
            std::unique_ptr<RHIResource> resource;
            Vector<PooledSubresource> subresources;
            uint32 unused_frames = 0;
            FrameResourceId frame_resource = invalid_frame_resource;

            RHIResourceDesc desc;
            RHIResourceState state = RHIResourceState::Undefined; // survives the frame, the graph resumes from it
            RHIMemoryCategory category = RHIMemoryCategory::Buffer;
            Size allocation_size = 0;
            Size allocation_alignment = 0;
            Size heap_index = ~(Size)0;
            Size heap_offset = 0;
        };

        struct Heap
        {
            RHIMemoryCategory category = RHIMemoryCategory::Buffer;
            Size size = 0;
            Size alignment = 0;
            std::unique_ptr<RHIMemoryBlock> block;
        };

        struct BufferUpload
        {
            FrameResourceId destination = invalid_frame_resource;
            Size destination_offset = 0;
            RHIResource* source = nullptr;
            Size source_offset = 0;
            Size size = 0;
        };

        PooledResource& CreatePooledResource(uint32 scope, const char* name);

        RHIDevice* device = nullptr;
        FrameContext* frame_context = nullptr;
        Vector<FrameResource> frame_resources; // per frame logical resources
        Vector<Pass> passes;
        Vector<BufferUpload> buffer_uploads;
        Size upload_pass_index = 0;
        bool has_upload_pass = false;
        UnorderedMap<uint64, PooledResource> pooled_resources; // keyed by StableHash of the name
        Vector<Heap> heaps;
    };
}
