#pragma once
#include "RuntimeExport.h"
#include "FrameContext.h"
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
        RHIDevice* device = nullptr;
        String shader_bin_root_path;
        RHIClearColor clear_color = { 0.0f, 0.3f, 0.3f, 1.0f };
        bool vsync_enabled = true;
    };

    constexpr RHIFormat HDR_COLOR_BUFFER_FORMAT = RHIFormat::R16G16B16A16Float;
    constexpr RHIFormat RENDERTARGET_BUFFER_FORMAT = RHIFormat::R8G8B8A8Unorm;
    constexpr RHIFormat DEPTH_BUFFER_FORMAT = RHIFormat::D32Float;

    class Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void Initialize(const RendererDesc& desc) = 0;
        virtual void BeginFrame(platform::Window& window, float delta_time) = 0;
        virtual void OnResize(platform::Window& window, uint32 width, uint32 height) = 0;
        virtual void Render(View& view) = 0;
#ifndef WON_SHIPPING
        virtual void RenderDebug2D() = 0;
#endif
        virtual void EndFrame() = 0;
        virtual void WaitIdle() = 0;
        virtual void Shutdown() = 0;
        virtual bool ReloadShaders() = 0;
        virtual RHIShader* GetShader(resource::ShaderId shader_id) const = 0;
        virtual void SetClearColor(const RHIClearColor& color) = 0;
        virtual RHIClearColor GetClearColor() const = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual void SetShadowResolutionScale(float scale) = 0;
        virtual bool GetCurrentBackBufferBinding(RHISubresourceBinding& out_binding) const = 0;


        inline FrameContext& GetFrameContext() { return frame_contexts[current_frame_slot]; };
        inline jobsystem::Context& GetRenderingWorkContext() { return rendering_work_context; };

        bool UpdateDefaultBuffer(FrameContext& frame_context, RHIResource& destination_buffer, const void* source_data, Size data_size, RHIResourceState final_state, Size destination_offset, RHICommandList& command_list)
        {
            const RHIResourceDesc& destination_desc = destination_buffer.GetDesc();
            Size upload_alignment = device->GetMinOffsetAlignment(destination_desc.buffer_desc);

            FrameUploadAllocation upload_allocation = {};
            if (!frame_context.AllocateFrameUpload(*device, data_size, upload_alignment, upload_allocation))
            {
                return false;
            }

            std::memcpy(upload_allocation.mapped_data, source_data, data_size);

            command_list.TransitionResource(destination_buffer, RHIResourceState::CopyDest);
            command_list.CopyBuffer(destination_buffer, destination_offset, *upload_allocation.buffer, upload_allocation.buffer_offset, data_size);
            command_list.TransitionResource(destination_buffer, final_state);
            return true;
        }

    protected:
        RHIDevice* device = nullptr;
        std::array<FrameContext, max_frames_in_flight> frame_contexts = {};
        jobsystem::Context rendering_work_context;

        uint32 current_frame_slot = 0;
        uint64 frame_count = 0;
    };

    WONENGINE_API std::unique_ptr<Renderer> CreateRenderer(const RendererDesc& desc);
}
