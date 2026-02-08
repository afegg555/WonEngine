#pragma once
#include "Renderer.h"

namespace won::rendering
{
    class ForwardRenderer final : public Renderer
    {
    public:
        void Initialize(const RendererDesc& desc) override;
        void BeginFrame(platform::Swapchain& swapchain) override;
        void Render(const View& view) override;
        void EndFrame() override;
        void Shutdown() override;

    private:
        std::shared_ptr<RHIDevice> device;
        platform::Swapchain* current_swapchain = nullptr;
    };
}
