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
        void Render(View& view) override;
#ifndef WON_SHIPPING
		void RenderDebug2D() override; // should be called after Render() to draw debug 2D elements on top of the scene
#endif
        void EndFrame() override;
        void WaitIdle() override;
        void Shutdown() override;
        bool ReloadShaders() override;
        std::shared_ptr<RHIShader> GetShader(resource::ShaderId shader_id) const override;
        void SetClearColor(const RHIClearColor& color) override;
        RHIClearColor GetClearColor() const override;
        void SetVSync(bool enabled) override;
        void SetShadowResolutionScale(float scale) override;
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
        bool CreateRenderTargetResources(FrameContext& frame_context);

        // gpu call
        bool BuildViewResources(FrameContext& frame_context, View& view, RHICommandList& command_list);
        bool UpdateFrameConstants(FrameContext& frame_context, const View& view, RHICommandList& command_list);
        bool DrawScene(const FrameContext& frame_context, const View& view, resource::RenderPassType pass, uint32 flags, RHICommandList& command_list);
        void UpdateDDGIProbe(FrameContext& frame_context, const View& view, RHICommandList& command_list);

        // debug draw
#ifndef WON_SHIPPING
        void BuildDebug3D(const View& view);
        void DrawDebug3D(RHICommandList& command_list);
        void DrawDebug2D(RHICommandList& command_list);
#endif

        // etc
        bool Update(View& view);
        void RenderForwardPath(View& view);
        void UpdateForwardLightList(View& view, RHICommandList& command_list);

        std::shared_ptr<RHIDevice> device;
        resource::ShaderCompilerOptions shader_compiler_options = {};
        resource::ShaderLibrary shader_library;

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

        std::shared_ptr<RHIPipeline> brdf_integration_pipeline;
        std::shared_ptr<RHIShader> brdf_integration_shader;
        std::shared_ptr<RHIResource> brdf_lut;
        RHISubresourceHandle brdf_lut_srv = {};
        RHISubresourceHandle brdf_lut_uav = {};

        std::shared_ptr<RHIPipeline> light_cull_pipeline;
        std::shared_ptr<RHIShader> light_cull_shader;

        std::shared_ptr<RHIPipeline> composite_pipeline;
        std::shared_ptr<RHIShader> composite_shader;

#ifndef WON_SHIPPING
        std::shared_ptr<RHIPipeline> debug_2d_pipeline;
        std::shared_ptr<RHIShader> debug_2d_vs;
        std::shared_ptr<RHIShader> debug_2d_ps;

        std::shared_ptr<RHIPipeline> debug_3d_pipeline;
        std::shared_ptr<RHIShader> debug_3d_vs;
        std::shared_ptr<RHIShader> debug_3d_ps;
        std::shared_ptr<RHIResource> debug_3d_buffer;
        RHISubresourceHandle debug_3d_buffer_srv = {};
#endif

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
        bool vsync_requested = true;
        float shadow_resolution_scale = 1.0f;

        std::array<RHISubresourceHandle, max_frames_in_flight> back_buffers_rtv = {};

        platform::Window* current_window = nullptr;
    };
}
