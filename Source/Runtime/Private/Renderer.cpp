#include "Renderer.h"
#include "ForwardRenderer.h"
#include "EventHandler.h"
#include "ShaderLibrary.h"

namespace won::rendering
{
    static std::shared_ptr<resource::ShaderLibrary> shader_library = std::make_shared<resource::ShaderLibrary>();

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
            renderer->Initialize(desc, shader_library);
        }

        return renderer;
    }

    void ReloadShaderLibrary(std::shared_ptr<RHIDevice> device)
    {
        eventhandler::Subscribe_Once(eventhandler::EVENT_THREAD_SAFE_POINT, [dev = device](uint64_t userdata) {
            
            auto context = dev->GetContext(rendering::RHIQueueType::Graphics);
            if (context)
            {
                context->WaitIdle();

                if (shader_library->LoadAllShaders())
                {
                    shader_library->BuildAllGraphicsPipelines(dev, RENDERTARGET_BUFFER_FORMAT, DEPTH_BUFFER_FORMAT, 1u);
                }
            }

        });

        return;
    }
}
