#include "Application.h"

#include "BuiltinTypeReflection.h"
#include "Renderer.h"
#include "Window.h"
#include "JobSystem.h"
#include "Platform.h"
#include "EventHandler.h"
#include "Input.h"
#include "Profiler.h"
#include "ScriptRuntime.h"

namespace won
{
    void Application::Initialize(const ApplicationDesc& desc)
    {
        reflection::RegisterBuiltinTypes();

        jobsystem::Initialize(desc.jobsystem_thread_count);

        window = platform::CreateNativeWindow(desc.window);
        if (!window)
        {
            is_running = false;
            return;
        }

        rendering::RHIDeviceDesc device_desc;
        device_desc.backend = desc.backend_type;
        device_desc.preference = desc.device_preference;
        device_desc.enable_debug_layer = true; // test

        device = rendering::CreateRHIDevice(device_desc);

        rendering::ReloadShaderLibrary(device);

        rendering::RendererDesc renderer_desc;
        renderer_desc.device = device;
        renderer = rendering::CreateRenderer(renderer_desc);

        script::ScriptRuntimeDesc script_desc = {};
        script_runtime = script::CreateScriptRuntime(script_desc);
        if (script_runtime && !script_runtime->Initialize())
        {
            script_runtime.reset();
        }

        views.clear();
        rendering::View default_view = {};
        default_view.viewport.width = 1280;
        default_view.viewport.height = 720;
        default_view.scissor.width = 1280;
        default_view.scissor.height = 720;
        AddView(default_view);

        frame_timer.Reset();
        is_first_frame = true;
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

        ProcessWindowResize();
        eventhandler::FireEvent(eventhandler::EVENT_THREAD_SAFE_POINT, 0);

        float dt = 0.0f;
        if (is_first_frame)
        {
            is_first_frame = false;
            frame_timer.Reset();
        }
        else
        {
            dt = static_cast<float>(frame_timer.ElapsedSeconds());
            frame_timer.Reset();
        }

        profiler::BeginFrame();
        Update(dt);
        Render();
        profiler::EndFrame();
    }
    
    void Application::Shutdown()
    {
        is_running = false;
        views.clear();

        if (renderer)
        {
            renderer->Shutdown();
            renderer.reset();
        }

        profiler::Shutdown();
        window.reset();

        if (script_runtime)
        {
            script_runtime->Shutdown();
            script_runtime.reset();
        }

        device.reset();
        jobsystem::ShutDown();
    }

    void Application::Update(float dt)
    {
        auto range = profiler::BeginRangeCPU("Update");
        if (window->IsFocused())
        {
            io::Update((WindowType)window->GetNativeHandle());
        }
        else
        {
            io::Reset();
        }
        ++update_index;

        for (const std::unique_ptr<rendering::View>& view_ptr : views)
        {
            if (!view_ptr)
            {
                continue;
            }

            rendering::View& view = *view_ptr;
            ecs::Scene* scene = view.scene;
            if (!scene)
            {
                continue;
            }

            if (scene->GetUpdateIndex() == update_index)
            {
                continue;
            }

            view.Update(dt);
            scene->SetUpdateIndex(update_index);
        }
        profiler::EndRange(range);
    }

    void Application::Render()
    {
        if (!renderer || !window)
        {
            return;
        }

        if (window->IsMinimized())
        {
            return;
        }

        //auto range = profiler::BeginRangeCPU("Render");
        renderer->BeginFrame(*window);
        RenderScene();
        RenderUI();
        renderer->EndFrame();
        //profiler::EndRange(range);
    }

    void Application::WaitIdle()
    {
        renderer->WaitIdle();
    }
    
    void Application::RenderScene()
    {
        if (renderer)
        {
            for (const std::unique_ptr<rendering::View>& view_ptr : views)
            {
                if (view_ptr)
                {
                    renderer->Render(*view_ptr);
                }
            }
        }
    }

    uint32 Application::AddView(const rendering::View& view)
    {
        views.push_back(std::make_unique<rendering::View>(view));
        return static_cast<uint32>(views.size() - 1);
    }

    rendering::View& Application::GetView(uint32 view_index)
    {
        return *views[view_index];
    }

    const rendering::View& Application::GetView(uint32 view_index) const
    {
        return *views[view_index];
    }

    void Application::RenderUI()
    {
        return;
    }

    void Application::OnWindowResized(int width, int height)
    {
        return;
    }

    void Application::ProcessWindowResize()
    {
        if (!window || !window->ConsumePendingResize())
        {
            return;
        }

        const int width = window->GetWidth();
        const int height = window->GetHeight();
        if (renderer)
        {
            renderer->OnResize(*window, static_cast<uint32>(width), static_cast<uint32>(height));
        }

        OnWindowResized(width, height);
    }

}
