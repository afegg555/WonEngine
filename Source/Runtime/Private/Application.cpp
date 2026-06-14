#include "Application.h"

#include "BuiltinTypeReflection.h"
#include "Renderer.h"
#include "Window.h"
#include "JobSystem.h"
#include "Platform.h"
#include "EventHandler.h"
#include "FileSystem.h"
#include "Input.h"
#include "Profiler.h"
#include "ScriptRuntime.h"
#include "PhysicsWorld.h"

namespace won
{
    void Application::Initialize(const ApplicationDesc& desc)
    {
        project_settings = desc.project_settings;

        reflection::RegisterBuiltinTypes();

        jobsystem::Initialize(desc.jobsystem_thread_count);
        won::physics::Initialize();

        platform::WindowDesc window_desc = {};
        window_desc.title = project_settings.window_title.c_str();
        window_desc.width = project_settings.window_width;
        window_desc.height = project_settings.window_height;
        window_desc.fullscreen = project_settings.window_fullscreen;
        window_desc.resizable = project_settings.window_resizable;
        window_desc.use_title_bar = project_settings.window_use_title_bar;
        window_desc.visible = project_settings.window_visible && !desc.defer_window_show;
        window = platform::CreateNativeWindow(window_desc);
        if (!window)
        {
            is_running = false;
            return;
        }

        rendering::RHIDeviceDesc device_desc;
        device_desc.backend = project_settings.backend_type;
        device_desc.preference = desc.device_preference;
        device_desc.enable_debug_layer = true; // test

        device = rendering::CreateRHIDevice(device_desc);

        rendering::RendererDesc renderer_desc;
        renderer_desc.device = device;
        if (!project_settings.project_root.empty())
        {
            renderer_desc.shader_bin_root_path = io::NormalizePath(io::CombinePath(project_settings.project_root, "CompiledShaders"));
        }
        else
        {
            renderer_desc.shader_bin_root_path = io::NormalizePath(io::CombinePath(io::GetExecutableDirectory(), "CompiledShaders"));
        }
        renderer_desc.vsync_enabled = project_settings.vsync_enabled;
        renderer = rendering::CreateRenderer(renderer_desc);

        script::ScriptRuntimeDesc script_desc = {};
        script_runtime = script::CreateScriptRuntime(script_desc);
        if (script_runtime && !script_runtime->Initialize())
        {
            script_runtime.reset();
        }

        if (!project_settings.input_action_map.empty())
        {
            const String content_root = project::GetContentRoot(project_settings);
            const String input_action_map_path = project::ResolveProjectContentPath(content_root, project_settings.input_action_map);
            io::LoadActionMap(input_action_map_path);
        }

        frame_timer.Reset();
        is_first_frame = true;
        is_running = true;
    }

    bool Application::IsRunning() const
    {
        return is_running;
    }

    void Application::ShowMainWindow()
    {
        if (window)
        {
            window->Show();
        }
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
        if (!project_settings.settings_path.empty())
        {
            project::SaveSettings(project_settings.settings_path, project_settings);
        }

        is_running = false;
        views.clear();

        if (renderer)
        {
            renderer->Shutdown();
            renderer.reset();
        }

        profiler::Shutdown();
        window.reset();
        io::Reset();
        io::ClearActionMap();

        if (script_runtime)
        {
            script_runtime->Shutdown();
            script_runtime.reset();
        }

        device.reset();
        won::physics::Shutdown();
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
        if (renderer)
        {
            renderer->WaitIdle();
        }
    }

    rendering::RHIDevice* Application::GetDevice()
    {
        return device.get();
    }

    script::ScriptRuntime* Application::GetScriptRuntime()
    {
        return script_runtime.get();
    }

    void Application::ClearViews()
    {
        views.clear();
    }
    
    void Application::RenderScene()
    {
        if (renderer)
        {
            for (const std::unique_ptr<rendering::View>& view_ptr : views)
            {
                if (view_ptr && view_ptr->scene)
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
        if (width <= 0 || height <= 0)
        {
            return;
        }

        for (const std::unique_ptr<rendering::View>& view_ptr : views)
        {
            if (!view_ptr)
            {
                continue;
            }

            rendering::View& view = *view_ptr;
            if (view.options.resize_policy == rendering::ViewResizePolicy::MatchWindow)
            {
                view.viewport.x = 0;
                view.viewport.y = 0;
                view.viewport.width = width;
                view.viewport.height = height;
                view.scissor = view.viewport;
            }

            if (!view.options.update_camera_aspect || view.viewport.width <= 0 || view.viewport.height <= 0 || !view.scene || view.camera_entity == ecs::INVALID_ENTITY)
            {
                continue;
            }

            ecs::CameraComponent* camera = view.scene->GetComponent<ecs::CameraComponent>(view.camera_entity);
            if (camera)
            {
                camera->SetAspectRatio(static_cast<float>(view.viewport.width) / static_cast<float>(view.viewport.height));
            }
        }
    }

    void Application::ProcessWindowResize()
    {
        if (!window || !window->ConsumePendingResize())
        {
            return;
        }

        const int width = window->GetWidth();
        const int height = window->GetHeight();
        project_settings.window_width = width;
        project_settings.window_height = height;
        if (renderer)
        {
            renderer->OnResize(*window, static_cast<uint32>(width), static_cast<uint32>(height));
        }

        OnWindowResized(width, height);
    }

}
