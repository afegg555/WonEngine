#pragma once
#include "FrameGraph.h"
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
        void BeginFrame(platform::Window& window, float delta_time) override;
        void OnResize(platform::Window& window, uint32 width, uint32 height) override;
        void Render(View& view) override;
#ifndef WON_SHIPPING
		void RenderDebug2D() override; // should be called after Render() to draw debug 2D elements on top of the scene
#endif
        void EndFrame() override;
        void WaitIdle() override;
        void Shutdown() override;
        bool ReloadShaders() override;
        RHIShader* GetShader(resource::ShaderId shader_id) const override;
        void SetClearColor(const RHIClearColor& color) override;
        RHIClearColor GetClearColor() const override;
        void SetVSync(bool enabled) override;
        void SetShadowResolutionScale(float scale) override;
        bool GetCurrentBackBufferBinding(RHISubresourceBinding& out_binding) const override;
        FrameGraph& GetFrameGraph() override;
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
        bool CreateBackBufferSubresources();

        // gpu call
        bool UploadSceneData(FrameContext& frame_context, GPUScene& gpu_scene);
        bool UploadViewData(FrameContext& frame_context, View& view);
        bool UpdateFrameConstants(FrameContext& frame_context, const View& view);
        bool DrawScene(const FrameContext& frame_context, const View& view, resource::RenderPassType pass, uint32 flags, RHICommandList& command_list, uint32 shadow_slice_index = 0);
        void UpdateDDGIProbe(FrameContext& frame_context, View& view, RHICommandList& command_list);
        void UpdateSkyCapture(GPUScene& gpu_scene, RHICommandList& command_list);

        // debug draw
#ifndef WON_SHIPPING
        void BuildDebug3D(const View& view);
        void DrawDebug3D(const View& view, RHICommandList& command_list);
        void DrawDebug2D(RHICommandList& command_list);
#endif

        // etc
        bool Update(View& view);
        void RenderForwardPath(View& view);

        resource::ShaderCompilerOptions shader_compiler_options = {};
        resource::ShaderLibrary shader_library;

        std::unique_ptr<RHIResource> shader_frame_buffer;
        RHISubresourceHandle shader_frame_buffer_cbv = {};


        std::unique_ptr<RHIResource> brdf_lut;
        RHISubresourceHandle brdf_lut_srv = {};
        RHISubresourceHandle brdf_lut_uav = {};

        std::unique_ptr<RHIResource> ltc_matrix_lut;
        RHISubresourceHandle ltc_matrix_lut_srv = {};
        std::unique_ptr<RHIResource> ltc_fresnel_lut;
        RHISubresourceHandle ltc_fresnel_lut_srv = {};

#ifndef WON_SHIPPING
        std::unique_ptr<RHIResource> debug_3d_buffer;
        RHISubresourceHandle debug_3d_buffer_srv = {};
#endif

        std::unique_ptr<RHICommandAllocator> enqueued_work_command_allocator;
        std::unique_ptr<RHICommandList> enqueued_work_command_list;
        std::unique_ptr<RHIFence> enqueued_work_fence;
        Vector<std::unique_ptr<RHIResource>> enqueued_work_scratch_resources;
        uint64 enqueued_work_fence_value = 0;
        bool enqueued_work_succeeded = true;
        bool enqueued_work_recorded = false;
        RHIClearColor clear_color = {};
        bool vsync_enabled = true;
        bool vsync_requested = true;
        float shadow_resolution_scale = 1.0f;
        double frame_time_seconds = 0.0;

		Vector<uint32> sort_upload_scratch; // not to allocate every frame !!
        FrameGraph frame_graph;

        std::array<RHISubresourceHandle, max_frames_in_flight> back_buffers_rtv = {};

        platform::Window* current_window = nullptr;
    };
}
