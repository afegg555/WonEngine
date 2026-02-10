#include "ForwardRenderer.h"

#include "Scene.h"

#include "Window.h"
#include "ShaderLibrary.h"

namespace won::rendering
{
    static won::resource::ShaderLibrary shader_library;

    void ForwardRenderer::Initialize(const RendererDesc& desc)
    {
        device = desc.device;

        shader_library.LoadAllShaders();
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
        device.reset();
    }
}
