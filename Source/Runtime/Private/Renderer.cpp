#include "Renderer.h"
#include "RendererInternal.h"

namespace won::rendering
{
    std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc)
    {
        std::shared_ptr<Renderer> renderer = std::make_shared<RendererInternal>();
        renderer->Initialize(desc);
        return renderer;
    }
}
