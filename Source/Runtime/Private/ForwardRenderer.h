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
        void OnResize(platform::Window& window, uint32 width, uint32 height) override;
        void Render(const View& view) override;
        void EndFrame() override;
        void WaitIdle() override;
        void Shutdown() override;
        RendererDebugState GetDebugState() const override;

        bool AllocateFrameUpload(FrameContext& frame_context, Size size, Size alignment, FrameUploadAllocation& out_allocation) override;
        bool BuildFrameContext(const View& view, FrameContext& frame_context) override;
        bool UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset = 0) override;
    private:
        
        enum DrawSceneFlags : uint32
        {
            DrawScene_Opaque = 1 << 0, // include opaque objects
            DrawScene_Transparent = 1 << 1, // include transparent objects
            DrawScene_ShadowCaster = 1 << 2, // include shadow casters only
        };

        bool BuildShadowCascades(const View& view);
        bool DrawScene(const View& view, const FrameContext& frame_context, resource::RenderPassType pass, uint32 flags);

        std::shared_ptr<RHIDevice> device;

        std::shared_ptr<RHIResource> shader_instance_default_buffer;
        RHISubresourceHandle shader_instance_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_geometry_default_buffer;
        RHISubresourceHandle shader_geometry_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_material_default_buffer;
        RHISubresourceHandle shader_material_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_light_default_buffer;
        RHISubresourceHandle shader_light_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_shadow_cascade_default_buffer;
        RHISubresourceHandle shader_shadow_cascade_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_bvh_node_default_buffer;
        RHISubresourceHandle shader_bvh_node_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_bvh_primitive_default_buffer;
        RHISubresourceHandle shader_bvh_primitive_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_frame_buffer;
        RHISubresourceHandle shader_frame_buffer_cbv = {};

        std::shared_ptr<RHIResource> shader_camera_buffer;
        RHISubresourceHandle shader_camera_buffer_cbv = {};

        std::shared_ptr<RHIResource> depth_buffer;
        RHISubresourceHandle depth_buffer_dsv = {};

        std::shared_ptr<RHIResource> shadow_map_atlas;
        RHISubresourceHandle shadow_map_atlas_dsv = {};
        RHISubresourceHandle shadow_map_atlas_srv = {};

        std::shared_ptr<RHIResource> ddgi_irradiance_texture;
        RHISubresourceHandle ddgi_irradiance_texture_srv = {};
        RHISubresourceHandle ddgi_irradiance_texture_uav = {};

        std::shared_ptr<RHIResource> ddgi_visibility_texture;
        RHISubresourceHandle ddgi_visibility_texture_srv = {};
        RHISubresourceHandle ddgi_visibility_texture_uav = {};

        std::shared_ptr<RHIPipeline> ddgi_probe_update_pipeline;
        std::shared_ptr<RHIShader> ddgi_probe_update_shader;
        RendererDebugState debug_state = {};

        uint2 shadow_map_atlas_size = { 0, 0 };
        uint3 ddgi_probe_counts = { 0, 0, 0 };
        std::array<RHISubresourceHandle, max_frames_in_flight> back_buffers_rtv = {};

        platform::Window* current_window = nullptr;
    };
}
