#include "Application.h"

#include "Renderer.h"
#include "Window.h"
#include "JobSystem.h"
#include "Platform.h"
#include "EventHandler.h"
#include "Input.h"

namespace won
{
    void Application::Initialize(const ApplicationDesc& desc)
    {
        jobsystem::Initialize(desc.jobsystem_thread_count);

        window = platform::CreateNativeWindow(desc.window);
        if (!window)
        {
            is_running = false;
            return;
        }

        rendering::RHIDeviceDesc device_desc;
        device_desc.backend = desc.backend_type;
        device_desc.enable_debug_layer = true; // test

        device = rendering::CreateRHIDevice(device_desc);

        rendering::ReloadShaderLibrary(device);

        rendering::RendererDesc renderer_desc;
        renderer_desc.type = desc.renderer_type;
        renderer_desc.device = device;
        renderer = rendering::CreateRenderer(renderer_desc);

        main_view.viewport.width = 1280;
        main_view.viewport.height = 720;
        main_view.scissor.width = 1280;
        main_view.scissor.height = 720;

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
                WaitIdle();
                Shutdown();
                return;
            }

            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
#endif
        eventhandler::FireEvent(eventhandler::EVENT_THREAD_SAFE_POINT, 0);

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
        io::Update((WindowType)window->GetNativeHandle());
        main_view.Update(dt);
    }

    void Application::Render()
    {
        if (!renderer || !window)
        {
            return;
        }

        renderer->BeginFrame(*window);
        RenderScene();
        RenderUI();
        renderer->EndFrame();
    }

    void Application::WaitIdle()
    {
        renderer->WaitIdle();
    }
    
    void Application::RenderScene()
    {
        if (renderer)
        {
            renderer->Render(main_view);
        }
    }

    void Application::RenderUI()
    {
        return;
    }

}
