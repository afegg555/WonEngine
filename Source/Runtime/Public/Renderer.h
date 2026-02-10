#pragma once
#include "RuntimeExport.h"
#include "RHIDevice.h"
#include "View.h"
#include "Window.h"

#include <memory>

namespace won::rendering
{
    enum class RendererType
    {
        Forward
    };

    struct RendererDesc
    {
        std::shared_ptr<RHIDevice> device;
        RendererType type = RendererType::Forward;
    };

    class WONENGINE_API Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void Initialize(const RendererDesc& desc) = 0;
        virtual void BeginFrame(platform::Window& window) = 0;
        virtual void Render(const View& view) = 0;
        virtual void EndFrame() = 0;
        virtual void Shutdown() = 0;
    };

    WONENGINE_API std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc);
}
