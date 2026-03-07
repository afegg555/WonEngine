#pragma once
#include "Renderer.h"
#include "RHISwapchain.h"
#include "Types.h"
#include "ShaderLibrary.h"

namespace won::rendering
{
    class ForwardRenderer final : public Renderer
    {
    public:
        void Initialize(const RendererDesc& desc, std::shared_ptr<resource::ShaderLibrary> shader_lib) override;
        void BeginFrame(platform::Window& window) override;
        void Render(const View& view) override;
        void EndFrame() override;
        void Shutdown() override;

    private:
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

        bool BuildFrameContext(const View& view, FrameContext& frame_context); 
        bool AllocateFrameUpload(FrameContext& frame_context, Size size, Size alignment, FrameUploadAllocation& out_allocation);
        bool UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset = 0);

        enum DrawSceneFlags : uint32
        {
            DrawScene_Opaque = 1 << 0, // include opaque objects
            DrawScene_Transparent = 1 << 1, // include transparent objects
        };

        bool DrawScene(const View& view, const FrameContext& frame_context, resource::RenderPassType pass, uint32 flags);

        std::shared_ptr<RHIDevice> device;
        std::array<FrameContext, max_frames_in_flight> frame_contexts = {};

        uint32 current_frame_slot = 0;
        uint64 frame_count = 0;

        std::shared_ptr<RHIResource> shader_instance_default_buffer;
        RHISubresourceHandle shader_instance_default_buffer_subresource = {};

        std::shared_ptr<RHIResource> shader_geometry_default_buffer;
        RHISubresourceHandle shader_geometry_default_buffer_subresource = {};

        std::shared_ptr<RHIResource> shader_material_default_buffer;
        RHISubresourceHandle shader_material_default_buffer_subresource = {};

        std::shared_ptr<RHIResource> shader_light_default_buffer;
        RHISubresourceHandle shader_light_default_buffer_subresource = {};

        std::shared_ptr<RHIResource> shader_frame_buffer;
        RHISubresourceHandle shader_frame_buffer_subresource = {};

        std::shared_ptr<RHIResource> shader_camera_buffer;
        RHISubresourceHandle shader_camera_buffer_subresource = {};

        std::shared_ptr<RHIResource> depth_buffer;
        RHISubresourceHandle depth_buffer_subresource = {};
        uint32 depth_buffer_width = 0;
        uint32 depth_buffer_height = 0;
        uint32 depth_buffer_sample_count = 1;

        platform::Window* current_window = nullptr;
    };
}
