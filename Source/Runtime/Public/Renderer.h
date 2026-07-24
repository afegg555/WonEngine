#pragma once
#include "RuntimeExport.h"
#include "RHIDevice.h"
#include "Types.h"
#include "View.h"
#include "Window.h"
#include "RHIResource.h"
#include "MathUtils.h"
#include "JobSystem.h"
#include "ShaderManifest.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace won::rendering
{
    struct RendererDesc
    {
        std::shared_ptr<RHIDevice> device;
        String shader_bin_root_path;
        RHIClearColor clear_color = { 0.0f, 0.3f, 0.3f, 1.0f };
        bool vsync_enabled = true;
    };

    struct RendererDebugOptions
    {
        bool ddgi_debug_enable = false;
        bool bvh_debug_enable = false;
    };

    struct RendererDebugDDGIState
    {
        struct DDGIProbe
        {
            float3 position = { 0.0f, 0.0f, 0.0f };
            float relocation = 0.0f;
            float validity = 1.0f;
        };

        bool gi_mode_ddgi = false;
        bool volume_active = false;
        bool irradiance_texture_allocated = false;
        bool irradiance_srv_valid = false;
        bool irradiance_uav_valid = false;
        bool visibility_texture_allocated = false;
        bool visibility_srv_valid = false;
        bool visibility_uav_valid = false;
        bool probe_data_buffer_allocated = false;
        bool probe_data_srv_valid = false;
        bool probe_data_uav_valid = false;
        bool history_valid = false;
        bool probe_update_pipeline_ready = false;
        bool probe_update_dispatched = false;

        ecs::Entity volume_entity = ecs::INVALID_ENTITY;
        uint3 probe_counts = { 0, 0, 0 };
        float3 volume_min = { 0.0f, 0.0f, 0.0f };
        float3 volume_max = { 0.0f, 0.0f, 0.0f };
        float3 probe_spacing = { 0.0f, 0.0f, 0.0f };
        uint32 total_probe_count = 0;
        uint3 dispatch_groups = { 0, 0, 0 };

        int irradiance_texture_srv = -1;
        int irradiance_texture_uav = -1;
        int visibility_texture_srv = -1;
        int visibility_texture_uav = -1;
        int probe_data_buffer_srv = -1;
        int probe_data_buffer_uav = -1;

        Vector<DDGIProbe> probes;
    };

    struct RendererDebugBVHState
    {
        struct BVHNode
        {
            float3 bounds_min = { 0.0f, 0.0f, 0.0f };
            float3 bounds_max = { 0.0f, 0.0f, 0.0f };
            bool is_leaf = false;
        };

        bool cpu_bvh_available = false;
        bool gpu_bvh_available = false;
        Vector<BVHNode> cpu_nodes;
        Vector<BVHNode> gpu_nodes;
    };

    struct RendererDebugState
    {
        RendererDebugDDGIState ddgi = {};
        RendererDebugBVHState bvh = {};

        uint32 draw_call_count = 0;
        uint32 total_renderable_count = 0;         // total before frustum culling
        uint32 visible_renderable_count = 0; // after frustum culling
    };

    constexpr RHIFormat HDR_COLOR_BUFFER_FORMAT = RHIFormat::R16G16B16A16Float;
    constexpr RHIFormat RENDERTARGET_BUFFER_FORMAT = RHIFormat::R8G8B8A8Unorm;
    constexpr RHIFormat DEPTH_BUFFER_FORMAT = RHIFormat::D32Float;

    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void Initialize(const RendererDesc& desc) = 0;
        virtual void BeginFrame(platform::Window& window) = 0;
        virtual void OnResize(platform::Window& window, uint32 width, uint32 height) = 0;
        virtual void Render(View& view) = 0;
        virtual void RenderDebugText() = 0;
        virtual void EndFrame() = 0;
        virtual void WaitIdle() = 0;
        virtual void Shutdown() = 0;
        virtual bool ReloadShaders() = 0;
        virtual std::shared_ptr<RHIShader> GetShader(resource::ShaderId shader_id) const = 0;
        virtual void SetClearColor(const RHIClearColor& color) = 0;
        virtual RHIClearColor GetClearColor() const = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual void SetShadowResolutionScale(float scale) = 0;
        virtual void SetDebugOptions(const RendererDebugOptions& options) = 0;
        virtual RendererDebugState GetDebugState() const = 0;
        virtual bool GetCurrentBackBufferBinding(RHISubresourceBinding& out_binding) const = 0;
        virtual bool GetCurrentDepthBufferBinding(RHISubresourceBinding& out_binding) const = 0;

        struct FrameCommandList
        {
            std::shared_ptr<RHICommandAllocator> command_allocator;
            std::shared_ptr<RHICommandList> command_list;
        };

        struct FrameUploadAllocation
        {
            std::shared_ptr<RHIResource> buffer;
            void* mapped_data = nullptr;
            Size buffer_offset = 0;
        };

        struct FrameContext
        {
            void BeginFrame()
            {
                if (fence_value > 0)
                {
                    profiler::ScopedRangeCPU wait_range("Frame Context Fence Wait");
                    fence->Wait(fence_value);
                    fence_value = 0;
                }

                {
                    std::scoped_lock lock(frame_upload_mutex);
                    frame_upload_offset = 0;
                }
                {
                    std::scoped_lock lock(deferred_res_removal_mutex);
                    deferred_res_removal.clear();
                }

                for (std::atomic<Size>& command_list_count : command_list_counts)
                {
                    command_list_count.store(0, std::memory_order_relaxed);
                }
            }

            RHICommandList* BeginCommandList(RHIDevice& device, RHIQueueType queue_type = RHIQueueType::Graphics)
            {
                const Size queue_index = static_cast<Size>(queue_type);
                const Size command_list_index = command_list_counts[queue_index].fetch_add(1, std::memory_order_relaxed);
                Vector<FrameCommandList>& typed_command_lists = command_lists[queue_index];
                {
                    std::scoped_lock lock(command_lists_mutex);
                    while (command_list_index >= typed_command_lists.size()) // should use while
                    {
                        Renderer::FrameCommandList new_command_list = {};
                        new_command_list.command_allocator = device.CreateCommandAllocator(queue_type);
                        new_command_list.command_list = device.CreateCommandList(queue_type);
                        if (!new_command_list.command_allocator || !new_command_list.command_list)
                        {
                            backlog::Post("failed to create frame command list", backlog::LogLevel::Error);
                            return nullptr;
                        }
                        typed_command_lists.push_back(std::move(new_command_list));
                    }

                    Renderer::FrameCommandList& frame_command_list = typed_command_lists[command_list_index];
                    frame_command_list.command_allocator->Reset();
                    frame_command_list.command_list->Begin(*frame_command_list.command_allocator);
                    return frame_command_list.command_list.get();
                }
            }

            uint64 SubmitCommandLists(RHIContext& context, RHIQueueType queue_type = RHIQueueType::Graphics)
            {
                const Size queue_index = static_cast<Size>(queue_type);
                Vector<FrameCommandList>& typed_command_lists = command_lists[queue_index];

                Vector<RHICommandList*> submitted_command_lists;
                const Size command_list_count = command_list_counts[queue_index].load(std::memory_order_relaxed);
                if (command_list_count == 0)
                {
                    return 0;
                }

                submitted_command_lists.reserve(command_list_count);
                for (Size command_list_index = 0; command_list_index < command_list_count; ++command_list_index)
                {
                    FrameCommandList& frame_command_list = typed_command_lists[command_list_index];
                    RHICommandList* command_list = frame_command_list.command_list.get();
                    command_list->End();
                    submitted_command_lists.push_back(command_list);
                }

                const uint64 submitted_fence_value = context.Submit(submitted_command_lists, fence.get());
                fence_value = submitted_fence_value;
                return submitted_fence_value;
            }

            bool AllocateFrameUpload(RHIDevice& device, Size size, Size alignment, FrameUploadAllocation& out_allocation)
            {
                std::scoped_lock lock(frame_upload_mutex);
                Size aligned_offset = 0;
                if (!frame_upload_buffer)
                {
                    RHIBufferDesc frame_upload_buffer_desc = {};
                    frame_upload_buffer_desc.size = math::Align((Size)1024 * 20, alignment);
                    frame_upload_buffer_desc.usage = RHIResourceUsage::Upload;
                    frame_upload_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::VertexBuffer | RHIBindFlags::IndexBuffer;
                    frame_upload_buffer = device.CreateBuffer(frame_upload_buffer_desc);
                    if (frame_upload_buffer)
                    {
                        frame_upload_buffer->SetName("Frame Upload Buffer");
                    }
                    else
                    {
                        return false;
                    }
                    frame_upload_offset = 0;
                }

                Size buffer_size = frame_upload_buffer->GetDesc().buffer_desc.size;
                aligned_offset = math::Align(frame_upload_offset, alignment);
                Size required_size = aligned_offset + size;

                if (buffer_size < required_size)
                {
                    RHIBufferDesc new_desc = frame_upload_buffer->GetDesc().buffer_desc;
                    new_desc.size = math::Align(required_size * 2, alignment);
                    RemoveResourceDeferred(frame_upload_buffer);
                    frame_upload_buffer = device.CreateBuffer(new_desc);
                    if (frame_upload_buffer)
                    {
                        frame_upload_buffer->SetName("Frame Upload Buffer");
                    }
                    else
                    {
                        return false;
                    }
                }
                frame_upload_offset = aligned_offset + size;

                void* mapped_data = frame_upload_buffer->GetMappedData();

                out_allocation.buffer = frame_upload_buffer;
                out_allocation.mapped_data = static_cast<uint8*>(mapped_data) + aligned_offset;
                out_allocation.buffer_offset = aligned_offset;
                return true;
            }

            void RemoveResourceDeferred(std::shared_ptr<RHIResource>& resource)
            {
                std::scoped_lock lock(deferred_res_removal_mutex);
                deferred_res_removal.push_back(resource);
                resource = nullptr;
            }

            Vector<FrameCommandList> command_lists[static_cast<Size>(RHIQueueType::Count)];
            std::atomic<Size> command_list_counts[static_cast<Size>(RHIQueueType::Count)] = {};
            std::mutex command_lists_mutex;
            std::mutex frame_upload_mutex;
            std::mutex deferred_res_removal_mutex;
            std::shared_ptr<RHIFence> fence;
            std::shared_ptr<RHIResource> shader_instance_sort_upload_buffer;
            std::shared_ptr<RHIResource> frame_upload_buffer;
            Size frame_upload_offset = 0;
            uint64 fence_value = 0;

            std::vector<std::shared_ptr<RHIResource>> deferred_res_removal;
        };

        inline FrameContext& GetFrameContext() { return frame_contexts[current_frame_slot]; };
        inline jobsystem::Context& GetRenderingWorkContext() { return rendering_work_context; };

        virtual bool UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset, RHICommandList& command_list) = 0;

    protected:
        std::array<FrameContext, max_frames_in_flight> frame_contexts = {};
        jobsystem::Context rendering_work_context;

        uint32 current_frame_slot = 0;
        uint64 frame_count = 0;
    };

    WONENGINE_API std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc);
}
