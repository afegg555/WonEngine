#include "Renderer.h"
#include "RendererInternal.h"

namespace won::rendering
{
    std::unique_ptr<Renderer> CreateRenderer(const RendererDesc& desc)
    {
        std::unique_ptr<Renderer> renderer = std::make_unique<RendererInternal>();
        renderer->Initialize(desc);
        return renderer;
    }
}
