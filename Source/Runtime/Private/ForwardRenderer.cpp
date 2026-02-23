#include "ForwardRenderer.h"

#include "Backlog.h"
#include "Scene.h"

#include "Window.h"
#include "ShaderLibrary.h"

namespace won::rendering
{
    static won::resource::ShaderLibrary shader_library;

    void ForwardRenderer::Initialize(const RendererDesc& desc)
    {
        device = desc.device;

        if (!shader_library.LoadAllShaders())
        {
            backlog::Post("ForwardRenderer failed to load test shaders", backlog::LogLevel::Error);
            return;
        }

        const std::shared_ptr<RHIShader> vertex_shader = shader_library.GetShader(resource::ShaderId::TestTriangleVS);
        const std::shared_ptr<RHIShader> pixel_shader = shader_library.GetShader(resource::ShaderId::TestRedPS);

        RHIGraphicsPipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = vertex_shader.get();
        pipeline_desc.pixel_shader = pixel_shader.get();
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;

        test_pipeline = device->CreateGraphicsPipeline(pipeline_desc);
        if (!test_pipeline)
        {
            backlog::Post("failed to create test graphics pipeline", backlog::LogLevel::Error);
            return;
        }
    }

    void ForwardRenderer::BeginFrame(platform::Window& window)
    {
        current_window = &window;
    }

    void ForwardRenderer::Render(const View& view)
    {
        (void)view;
        // TODO: build render snapshot and submit passes.
    }

    void ForwardRenderer::EndFrame()
    {
    }

    void ForwardRenderer::Shutdown()
    {
        current_window = nullptr;
        test_pipeline = nullptr;
        device.reset();
    }
}
