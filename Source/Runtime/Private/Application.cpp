#include "Application.h"

#include "Renderer.h"
#include "Window.h"
#include "JobSystem.h"
#include "Platform.h"

 
namespace won
{
    void Application::Initialize(const ApplicationDesc& desc)
    {
        window = platform::CreateNativeWindow(desc.window);
        if (!window)
        {
            is_running = false;
            return;
        }

        rendering::RHIDeviceDesc device_desc;
        device_desc.backend = desc.backend_type;
        device = rendering::CreateRHIDevice(device_desc);

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

#if defined(_WIN32)
        MSG msg = {};
        while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
            {
                is_running = false;
                return;
            }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
#endif

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

        window.reset();
        device.reset();

        jobsystem::ShutDown();
    }

    void Application::Update(float dt)
    {

    }

    void Application::Render()
    {
        if (!renderer || !window)
        {
            return;
        }
        renderer->BeginFrame(*window);
        renderer->Render(main_view);
        renderer->EndFrame();
    }

}
