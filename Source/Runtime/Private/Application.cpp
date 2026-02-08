#include "Application.h"

#include "Renderer.h"
#include "Swapchain.h"
#include "JobSystem.h"

 
namespace won
{
    void Application::Initialize(const ApplicationDesc& desc)
    {
        swapchain = platform::CreateSwapchain(desc.swapchain);

        rendering::RHIDeviceDesc device_desc;
        device_desc.backend = desc.backend_type;
        std::shared_ptr<rendering::RHIDevice> device;
        //device = rendering::CreateRHIDevice(device_desc);

        rendering::RendererDesc renderer_desc;
        renderer_desc.type = desc.renderer_type;
        renderer_desc.device = device;
        renderer = rendering::CreateRenderer(renderer_desc);

        main_view = {};

        jobsystem::Initialize(desc.jobsystem_thread_count);

        is_running = true;
    }

    bool Application::IsRunning() const
    {
        return is_running;
    }

    void Application::Run()
    {
        if (!is_running)
            return;

        Update(0.f);
        Render();
    }
    
    void Application::Shutdown()
    {
        is_running = false;

        if (renderer)
        {
            renderer->Shutdown();
            renderer.reset();
        }

        swapchain.reset();
        jobsystem::ShutDown();
    }

    void Application::Update(float dt)
    {

    }

    void Application::Render()
    {
        renderer->BeginFrame(*swapchain);
        renderer->Render(main_view);
        renderer->EndFrame();
    }

}
