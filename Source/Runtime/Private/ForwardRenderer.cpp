#include "ForwardRenderer.h"

#include "Scene.h"
#include "Swapchain.h"

namespace won::rendering
{
    void ForwardRenderer::Initialize(const RendererDesc& desc)
    {
        device = desc.device;
    }

    void ForwardRenderer::BeginFrame(platform::Swapchain& swapchain)
    {
        current_swapchain = &swapchain;
    }

    void ForwardRenderer::Render(const View& view)
    {
        (void)view;
        (void)current_swapchain;
        // TODO: build render snapshot and submit passes.
    }

    void ForwardRenderer::EndFrame()
    {
    }

    void ForwardRenderer::Shutdown()
    {
        current_swapchain = nullptr;
        device.reset();
    }
}
