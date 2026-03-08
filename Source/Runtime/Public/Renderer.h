#pragma once
#include "RuntimeExport.h"
#include "RHIDevice.h"
#include "View.h"
#include "Window.h"
#include "ShaderLibrary.h"
#include "RHIResource.h"

#include <memory>

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

    constexpr RHIFormat RENDERTARGET_BUFFER_FORMAT = RHIFormat::R8G8B8A8Unorm;
    constexpr RHIFormat DEPTH_BUFFER_FORMAT = RHIFormat::D32Float;

    class WONENGINE_API Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void Initialize(const RendererDesc& desc, std::shared_ptr<resource::ShaderLibrary> shader_lib) = 0;
        virtual void BeginFrame(platform::Window& window) = 0;
        virtual void Render(const View& view) = 0;
        virtual void EndFrame() = 0;
        virtual void Shutdown() = 0;

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
        };

        struct FrameUploadAllocation
        {
            void* mapped_data = nullptr;
            Size buffer_offset = 0;
        };

        inline FrameContext& GetFrameContext() { return frame_contexts[current_frame_slot]; };

        virtual bool AllocateFrameUpload(FrameContext& frame_context, Size size, Size alignment, FrameUploadAllocation& out_allocation) { return false; };
        virtual bool BuildFrameContext(const View& view, FrameContext& frame_context) { return false; };
        virtual bool UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset = 0) { return false; };

    protected:
        std::shared_ptr<resource::ShaderLibrary> shader_library;
        std::array<FrameContext, max_frames_in_flight> frame_contexts = {};

        uint32 current_frame_slot = 0;
        uint64 frame_count = 0;
    };

    WONENGINE_API std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc);
    WONENGINE_API void ReloadShaderLibrary(std::shared_ptr<RHIDevice> device);
}
