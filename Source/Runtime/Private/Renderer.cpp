#include "Renderer.h"
#include "ForwardRenderer.h"

namespace won::rendering
{
    std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc)
    {
        std::shared_ptr<Renderer> renderer;
        switch (desc.type)
        {
        case RendererType::Forward:
        default:
            renderer = std::make_shared<ForwardRenderer>();
            break;
        }

        if (renderer)
        {
            renderer->Initialize(desc);
        }

        return renderer;
    }
}
