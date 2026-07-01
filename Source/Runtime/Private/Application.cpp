#include "Application.h"

#include "Backlog.h"
#include "ResourceExtension.h"
#include "BuiltinTypeReflection.h"
#include "CameraComponent.h"
#include "JsonArchive.h"
#include "Renderer.h"
#include "ResourceAsset.h"
#include "SceneSerializer.h"
#include "Window.h"
#include "JobSystem.h"
#include "Platform.h"
#include "EventHandler.h"
#include "FileSystem.h"
#include "Input.h"
#include "Profiler.h"
#include "ScriptRuntime.h"
#include "StringUtils.h"
#include "PhysicsWorld.h"

namespace won
{
	// currently uses a hash of the schema filename to derive the save file name... maybe changed ??
    static String DeriveGameDataSaveFile(const String& schema_path)
    {
        const String stem = io::ReplaceExtension(io::GetFilename(schema_path), "");
        const uint64 h = utils::Hash(stem);
        return std::to_string(h) + "." + resource::game_data_schema_extension;
    }

    void Application::Initialize(const ApplicationDesc& desc)
    {
        project_settings = desc.project_settings;

        if (!project_settings.project_name.empty())
        {
            const String log_dir = io::CombinePath(io::GetCacheDirectory(project_settings.project_name), "Logs");
            io::CreateFolder(log_dir);
            backlog::SetLogFile(io::CombinePath(log_dir, utils::GetCurrentDateTime() + ".log"));
        }

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

        audio_mixer = std::make_unique<won::audio::AudioMixer>(desc.audio.sample_rate, desc.audio.channel_count);
        audio_driver = won::audio::CreateAudioDriver();
        if (audio_driver)
        {
            if (audio_driver->Start(desc.audio, won::audio::AudioMixer::StaticMixCallback, audio_mixer.get()))
            {
                // sample rate and channel count may be adjusted by the driver internally
                audio_mixer->SetFormat(audio_driver->GetSampleRate(), audio_driver->GetChannelCount());
                backlog::Post("[Audio] driver started: " + std::to_string(audio_driver->GetSampleRate()) + "Hz, " + std::to_string(audio_driver->GetChannelCount()) + "ch");
            }
            else
            {
                backlog::Post("[Audio] driver failed to start", backlog::LogLevel::Error);
            }
        }
        else
        {
            backlog::Post("[Audio] no audio driver available", backlog::LogLevel::Error);
        }

        script::ScriptRuntimeDesc script_desc = {};
        script_desc.game_data = &game_data;
        script_desc.audio_mixer = audio_mixer.get();
        script_desc.content_root = project::GetContentRoot(project_settings);
        script_runtime = script::CreateScriptRuntime(script_desc);
        if (script_runtime && !script_runtime->Initialize())
        {
            script_runtime.reset();
        }

        if (!project_settings.input_action_map.empty())
        {
            const String content_root = project::GetContentRoot(project_settings);
            const String input_action_map_path = project::ResolveProjectContentPath(content_root, project_settings.input_action_map);
            if (io::LoadActionMap(input_action_map_path))
            {
                backlog::Post("[Input] action map loaded: " + input_action_map_path);
            }
            else if (!io::IsFile(input_action_map_path))
            {
                // Missing action maps are optional because raw input remains available.
                backlog::Post("[Input] action map not found, skipping: " + input_action_map_path, backlog::LogLevel::Warning);
            }
            else
            {
                backlog::Post("[Input] failed to parse action map: " + input_action_map_path, backlog::LogLevel::Error);
            }
        }

        if (!project_settings.game_data_schema.empty())
        {
            const String content_root = project::GetContentRoot(project_settings);
            const String schema_path = project::ResolveProjectContentPath(content_root, project_settings.game_data_schema);
            const String save_file = DeriveGameDataSaveFile(project_settings.game_data_schema);
            if (game_data.LoadSchema(schema_path.c_str()))
            {
                backlog::Post("[GameData] schema loaded: " + schema_path);
            }
            else
            {
                backlog::Post("[GameData] failed to load schema: " + schema_path, backlog::LogLevel::Error);
            }
            if (game_data.Load(project_settings.project_name.c_str(), save_file.c_str()))
            {
                backlog::Post("[GameData] save loaded: " + save_file);
            }
            else
            {
                backlog::Post("[GameData] no save found, using defaults: " + save_file, backlog::LogLevel::Default);
            }
        }

        frame_timer.Reset();
        is_first_frame = true;
        is_running = true;

        scene_load_handle = eventhandler::Subscribe(
            eventhandler::EVENT_SCENE_LOAD,
            [this](const won::function::Value& payload)
            {
                if (payload.type != won::ValueType::String || !payload.string_value)
                    return;
                const String path = won::io::CombinePath(
                    won::project::GetContentRoot(project_settings), payload.string_value);
                backlog::Post("[SceneTransition] requested: " + path, backlog::LogLevel::Default);
                eventhandler::SubscribeOnce(
                    eventhandler::EVENT_THREAD_SAFE_POINT,
                    [this, path](const won::function::Value&)
                    {
                        backlog::Post("[SceneTransition] loading: " + path, backlog::LogLevel::Default);
						// currently we only support one scene at a time, so we will load the scene into the first view
                        for (const std::unique_ptr<rendering::View>& view_ptr : views)
                        {
                            if (!view_ptr || !view_ptr->scene)
                                continue;
                            WaitIdle();
                            serialize::JsonArchive archive(serialize::ArchiveMode::Read);
                            if (archive.LoadFromFile(path))
                            {
                                view_ptr->scene->ClearEntities();
                                serialize::Serialize(archive, *view_ptr->scene);
                                resource::LoadSceneResources(*view_ptr->scene, *device, project::GetContentRoot(project_settings));
                                backlog::Post("[SceneTransition] complete: " + path, backlog::LogLevel::Default);
                            }
                            else
                            {
                                backlog::Post("[SceneTransition] failed to load archive: " + path, backlog::LogLevel::Error);
                            }
                            view_ptr->camera_entity = ecs::INVALID_ENTITY;
                            for (ecs::Entity e : view_ptr->scene->GetEntities())
                            {
                                if (view_ptr->scene->GetComponent<ecs::CameraComponent>(e))
                                {
                                    view_ptr->camera_entity = e;
                                    break;
                                }
                            }
                            break;
                        }
                    });
            });
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
        eventhandler::FireEvent(eventhandler::EVENT_THREAD_SAFE_POINT);

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
        if (!project_settings.game_data_schema.empty() && !project_settings.project_name.empty())
        {
            const String save_file = DeriveGameDataSaveFile(project_settings.game_data_schema);
            if (game_data.Save(project_settings.project_name.c_str(), save_file.c_str()))
            {
                backlog::Post("[GameData] saved: " + save_file);
            }
            else
            {
                backlog::Post("[GameData] failed to save: " + save_file, backlog::LogLevel::Error);
            }
        }

        if (!project_settings.settings_path.empty())
        {
            project::SaveSettings(project_settings.settings_path, project_settings);
        }

        is_running = false;
        views.clear();

        if (audio_driver)
        {
            audio_driver->Stop();
            audio_driver.reset();
        }
        audio_mixer.reset();

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

    won::audio::AudioMixer* Application::GetAudioMixer()
    {
        return audio_mixer.get();
    }

    game::GameData* Application::GetGameData()
    {
        return &game_data;
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
        // Project settings provide the default render/view options (anti-aliasing, etc.).
        views.back()->options.aa_mode = project_settings.aa_mode;
        views.back()->options.tonemap_mode = project_settings.tonemap_mode;
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
