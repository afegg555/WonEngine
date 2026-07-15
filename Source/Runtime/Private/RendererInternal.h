#pragma once
#include "Renderer.h"
#include "RHISwapchain.h"
#include "Types.h"
#include "ShaderLibrary.h"
#include "ShaderInterop_Renderer.h"

namespace won::rendering
{
    class RendererInternal final : public Renderer
    {
    public:
        void Initialize(const RendererDesc& desc) override;
        void BeginFrame(platform::Window& window) override;
        void OnResize(platform::Window& window, uint32 width, uint32 height) override;
        void Render(const View& view) override;
        void RenderDebugText() override;
        void EndFrame() override;
        void WaitIdle() override;
        void Shutdown() override;
        bool ReloadShaders() override;
        std::shared_ptr<RHIShader> GetShader(resource::ShaderId shader_id) const override;
        void SetClearColor(const RHIClearColor& color) override;
        RHIClearColor GetClearColor() const override;
        void SetDebugOptions(const RendererDebugOptions& options) override;
        RendererDebugState GetDebugState() const override;
        bool GetCurrentBackBufferBinding(RHISubresourceBinding& out_binding) const override;
        bool GetCurrentDepthBufferBinding(RHISubresourceBinding& out_binding) const override;
        bool UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset, RHICommandList& command_list) override;
    private:
        
        enum DrawSceneFlags : uint32
        {
            DrawScene_Opaque = 1 << 0, // include opaque objects
            DrawScene_Transparent = 1 << 1, // include transparent objects
            DrawScene_Primitive = 1 << 2, // include line/point ..
            DrawScene_3DSprite = 1 << 3, // include sprite3d, font3d
            DrawScene_2DSprite = 1 << 4, // include sprite2d, font2d
            DrawScene_Decal = 1 << 5, // include projected decals
        };

        // resource creation
        bool CreateDDGIResources(FrameContext& frame_context, const ShaderDDGIVolume& ddgi_volume);
        void ReleaseDDGIResources(FrameContext& frame_context);
        bool CreateShadowMapAtlasResources(FrameContext& frame_context, const ecs::Scene::RenderData& render_data);
        bool CreateRenderTargetResources(FrameContext& frame_context);

        // gpu call
        bool UpdateSceneGPUData(FrameContext& frame_context, const ecs::Scene::RenderData& render_data, const View& view, RHICommandList& command_list);
        bool UpdateFrameConstants(FrameContext& frame_context, const View& view, const ecs::Scene::RenderData& render_data, RHICommandList& command_list);
        bool DrawScene(const FrameContext& frame_context, const View& view, resource::RenderPassType pass, uint32 flags, RHICommandList& command_list);
        void UpdateDDGIProbe(FrameContext& frame_context, const ShaderEnvironment& environment_lighting, const ShaderDDGIVolume& ddgi_volume, const RHISubresourceBinding& shader_frame_binding, const RHISubresourceBinding& shader_camera_binding, RHICommandList& command_list);
        void DrawDebugText(const RHISubresourceBinding& back_buffer_binding, RHICommandList& command_list);

        // etc
        bool BuildShadowCascades(const View& view);
        void UpdateDebugState(const View& view, const ecs::Scene::RenderData& render_data);
        void RenderForwardPath(const View& view);

        std::shared_ptr<RHIDevice> device;
        resource::ShaderCompilerOptions shader_compiler_options = {};
        resource::ShaderLibrary shader_library;

        std::shared_ptr<RHIResource> shader_instance_default_buffer;
        RHISubresourceHandle shader_instance_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_particle_default_buffer;
        RHISubresourceHandle shader_particle_default_buffer_srv = {};
        std::shared_ptr<RHIResource> shader_decal_default_buffer;
        RHISubresourceHandle shader_decal_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_instance_sort_default_buffer;
        RHISubresourceHandle shader_instance_sort_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_geometry_default_buffer;
        RHISubresourceHandle shader_geometry_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_material_default_buffer;
        RHISubresourceHandle shader_material_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_bone_matrix_default_buffer;
        RHISubresourceHandle shader_bone_matrix_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_light_default_buffer;
        RHISubresourceHandle shader_light_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_shadow_cascade_default_buffer;
        RHISubresourceHandle shader_shadow_cascade_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_bvh_node_default_buffer;
        RHISubresourceHandle shader_bvh_node_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_bvh_instance_default_buffer;
        RHISubresourceHandle shader_bvh_instance_default_buffer_srv = {};

        std::shared_ptr<RHIResource> shader_frame_buffer;
        RHISubresourceHandle shader_frame_buffer_cbv = {};

        std::shared_ptr<RHIResource> shader_camera_buffer;
        RHISubresourceHandle shader_camera_buffer_cbv = {};

        std::shared_ptr<RHIResource> depth_buffer;
        RHISubresourceHandle depth_buffer_dsv = {};
        RHISubresourceHandle depth_buffer_srv = {};

        // Offscreen HDR color ping-pong buffers. The scene always renders into color_buffer[0];
        // the post chain ping-pongs between the two and the result is composited to the backbuffer.
        std::shared_ptr<RHIResource> color_buffer[2];
        RHISubresourceHandle color_buffer_rtv[2] = {};
        RHISubresourceHandle color_buffer_srv[2] = {};
        RHISubresourceHandle color_buffer_uav[2] = {};
        std::shared_ptr<RHIPipeline> fxaa_pipeline;
        std::shared_ptr<RHIShader> fxaa_shader;

        std::shared_ptr<RHIPipeline> tonemap_pipeline;
        std::shared_ptr<RHIShader> tonemap_shader;

        std::shared_ptr<RHIPipeline> luminance_reduce_pipeline;
        std::shared_ptr<RHIShader> luminance_reduce_shader;
        std::shared_ptr<RHIPipeline> luminance_resolve_pipeline;
        std::shared_ptr<RHIShader> luminance_resolve_shader;
        std::shared_ptr<RHIResource> luminance_partial_buffer;
        RHISubresourceHandle luminance_partial_buffer_uav = {};
        RHISubresourceHandle luminance_partial_buffer_srv = {};
        std::shared_ptr<RHIResource> luminance_buffer;
        RHISubresourceHandle luminance_buffer_uav = {};
        std::shared_ptr<RHIResource> luminance_readback_buffer;
        bool auto_exposure_active = false;

        std::shared_ptr<RHIPipeline> composite_pipeline;
        std::shared_ptr<RHIShader> composite_shader;

        std::shared_ptr<RHIPipeline> debug_text_pipeline;
        std::shared_ptr<RHIShader> debug_text_vs;
        std::shared_ptr<RHIShader> debug_text_ps;

        std::shared_ptr<RHIResource> shadow_map_atlas;
        RHISubresourceHandle shadow_map_atlas_dsv = {};
        RHISubresourceHandle shadow_map_atlas_srv = {};

        std::shared_ptr<RHIResource> ddgi_irradiance_texture;
        RHISubresourceHandle ddgi_irradiance_texture_srv = {};
        RHISubresourceHandle ddgi_irradiance_texture_uav = {};
        std::shared_ptr<RHIResource> ddgi_irradiance_history_texture;
        RHISubresourceHandle ddgi_irradiance_history_texture_srv = {};

        std::shared_ptr<RHIResource> ddgi_visibility_texture;
        RHISubresourceHandle ddgi_visibility_texture_srv = {};
        RHISubresourceHandle ddgi_visibility_texture_uav = {};
        std::shared_ptr<RHIResource> ddgi_visibility_history_texture;
        RHISubresourceHandle ddgi_visibility_history_texture_srv = {};

        std::shared_ptr<RHIResource> ddgi_probe_data_buffer;
        RHISubresourceHandle ddgi_probe_data_buffer_srv = {};
        RHISubresourceHandle ddgi_probe_data_buffer_uav = {};
        std::shared_ptr<RHIResource> ddgi_probe_data_history_buffer;
        RHISubresourceHandle ddgi_probe_data_history_buffer_srv = {};

        std::shared_ptr<RHIPipeline> ddgi_probe_update_pipeline;
        std::shared_ptr<RHIShader> ddgi_probe_update_shader;
        std::shared_ptr<RHICommandAllocator> enqueued_work_command_allocator;
        std::shared_ptr<RHICommandList> enqueued_work_command_list;
        std::shared_ptr<RHIFence> enqueued_work_fence;
        Vector<std::shared_ptr<RHIResource>> enqueued_work_scratch_resources;
        uint64 enqueued_work_fence_value = 0;
        bool enqueued_work_succeeded = true;
        bool enqueued_work_recorded = false;
        RHIClearColor clear_color = {};
        bool vsync_enabled = true;
        RendererDebugOptions debug_options = {};
        RendererDebugState debug_state = {};

        uint2 shadow_map_atlas_size = { 0, 0 };
        uint3 ddgi_probe_counts = { 0, 0, 0 };
        float3 ddgi_probe_spacing = { 0.0f, 0.0f, 0.0f };
        float3 ddgi_volume_min = { 0.0f, 0.0f, 0.0f };
        float ddgi_max_distance = 0.0f;
        uint32 ddgi_probe_update_offset = 0;
        bool ddgi_history_valid = false;
        std::array<RHISubresourceHandle, max_frames_in_flight> back_buffers_rtv = {};

        platform::Window* current_window = nullptr;
    };
}
