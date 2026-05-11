#include "Renderer.h"
#include "RendererInternal.h"
#include "Backlog.h"
#include "EventHandler.h"
#include "ShaderManifest.h"
#include "ShaderLibrary.h"

namespace won::rendering
{
    std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc)
    {
        std::shared_ptr<Renderer> renderer = std::make_shared<RendererInternal>();
        renderer->Initialize(desc);
        return renderer;
    }

    void ReloadShaderLibrary(std::shared_ptr<RHIDevice> device)
    {
        auto reload_shader_library = [dev = device](bool wait_idle) -> bool {
            if (!dev)
            {
                return false;
            }
            if (wait_idle)
            {
                auto context = dev->GetContext(rendering::RHIQueueType::Graphics);
                if (!context)
                {
                    return false;
                }
                context->WaitIdle();
            }

            resource::ShaderLibrary& shader_library = resource::GetShaderLibrary();
            if (!shader_library.LoadManifest(resource::GetDefaultShaderManifest()))
            {
                return false;
            }

            return shader_library.BuildAllGraphicsPipelines(dev, RENDERTARGET_BUFFER_FORMAT, DEPTH_BUFFER_FORMAT, 1u);
        };

        static bool initial_shader_load_requested = false;
        if (!initial_shader_load_requested)
        {
            initial_shader_load_requested = true;
            reload_shader_library(false);
            return;
        }

        eventhandler::Subscribe_Once(eventhandler::EVENT_THREAD_SAFE_POINT, [reload_shader_library](uint64_t userdata) {
            reload_shader_library(true);
        });

        return;
    }
}
