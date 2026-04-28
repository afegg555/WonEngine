#pragma once
#include "RuntimeExport.h"
#include "RHIDevice.h"
#include "View.h"
#include "Window.h"
#include "RHIResource.h"

#include <memory>

namespace won::resource
{
    class ShaderLibrary;
}
namespace won::rendering
{
    enum class RendererType
    {
        Forward
    };

    struct RendererDesc
    {
        std::shared_ptr<RHIDevice> device;
        RendererType type = RendererType::Forward;
    };

    struct RendererDebugOptions
    {
        bool ddgi_debug_enable = false;
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

        std::shared_ptr<RHIResource> probe_data_readback_buffer;
        bool probe_data_readback_valid = false;
        Vector<DDGIProbe> probes;
    };

    struct RendererDebugState
    {
        RendererDebugDDGIState ddgi = {};
    };

    constexpr RHIFormat RENDERTARGET_BUFFER_FORMAT = RHIFormat::R8G8B8A8Unorm;
    constexpr RHIFormat DEPTH_BUFFER_FORMAT = RHIFormat::D32Float;

    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void Initialize(const RendererDesc& desc) = 0;
        virtual void BeginFrame(platform::Window& window) = 0;
        virtual void OnResize(platform::Window& window, uint32 width, uint32 height) = 0;
        virtual void Render(const View& view) = 0;
        virtual void EndFrame() = 0;
        virtual void WaitIdle() = 0;
        virtual void Shutdown() = 0;
        virtual void SetDebugOptions(const RendererDebugOptions& options) = 0;
        virtual RendererDebugState GetDebugState() const = 0;

        struct FrameContext
        {
            std::shared_ptr<RHICommandAllocator> command_allocator;
            std::shared_ptr<RHICommandList> command_list;
            std::shared_ptr<RHIFence> fence;
            std::shared_ptr<RHIResource> shader_instance_upload_buffer;
            std::shared_ptr<RHIResource> shader_geometry_upload_buffer;
            std::shared_ptr<RHIResource> shader_material_upload_buffer;
            std::shared_ptr<RHIResource> shader_light_upload_buffer;
            std::shared_ptr<RHIResource> frame_upload_buffer;
            Size frame_upload_offset = 0;
            uint64 fence_value = 0;

            std::vector<std::shared_ptr<RHIResource>> deferred_res_removal;
        };

        struct FrameUploadAllocation
        {
            void* mapped_data = nullptr;
            Size buffer_offset = 0;
        };

        inline FrameContext& GetFrameContext() { return frame_contexts[current_frame_slot]; };

        virtual bool AllocateFrameUpload(FrameContext& frame_context, Size size, Size alignment, FrameUploadAllocation& out_allocation) = 0;
        virtual bool BuildFrameContext(const View& view, FrameContext& frame_context) = 0;
        virtual bool UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset = 0) = 0;

    protected:
        std::array<FrameContext, max_frames_in_flight> frame_contexts = {};

        uint32 current_frame_slot = 0;
        uint64 frame_count = 0;
    };

    WONENGINE_API std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc);
    WONENGINE_API void ReloadShaderLibrary(std::shared_ptr<RHIDevice> device);
}
