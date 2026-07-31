#include "Application.h"

#include "Backlog.h"
#include "ResourceExtension.h"
#include "BuiltinTypeReflection.h"
#include "CameraComponent.h"
#include "BuiltinFont.h"
#include "Console.h"
#include "JsonArchive.h"
#include "Localization.h"
#include "Renderer.h"
#include "ResourceAsset.h"
#include "RenderingUtils.h"
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
#include "Version.h"

namespace won
{
#ifndef WON_SHIPPING
    static console::ConsoleVariable r_debug_viewmode("r.debug.viewmode", 0, "exclusive debug view mode: 0=Lit 1=Unlit 2=BaseColor 3=WorldNormal 4=Roughness 5=Metallic 6=LightComplexity 7=ShadowCascades 8=Wireframe 9=Overdraw", console::ConsoleVariableFlagNone);
    static console::ConsoleVariable r_debug_show_bvh("r.debug.show.bvh", false, "overlay scene BVH bounds", console::ConsoleVariableFlagNone);
    static console::ConsoleVariable r_debug_show_ddgi("r.debug.show.ddgi", false, "overlay DDGI volume and probes", console::ConsoleVariableFlagNone);
    static console::ConsoleVariable r_debug_show_colliders("r.debug.show.colliders", false, "overlay physics collider bounds", console::ConsoleVariableFlagNone);
    static console::ConsoleVariable r_debug_freeze_culling("r.debug.freeze_culling", false, "freeze the culling frustum at its current state", console::ConsoleVariableFlagNone);
#endif

	// currently uses a hash of the schema filename to derive the save file name... maybe changed ??
    static String DeriveGameDataSaveFile(const String& schema_path)
    {
        const String stem = io::ReplaceExtension(io::GetFilename(schema_path), "");
        const uint64 h = utils::Hash(stem);
        return std::to_string(h) + "." + resource::game_data_schema_extension;
    }

    void Application::ApplyProjectSettings(const project::ProjectSettings& settings)
    {
        const String content_root = project::GetContentRoot(settings);

        locale::Initialize(content_root, settings.packaged_languages, settings.default_language);
        if (!settings.packaged_languages.empty() || !settings.default_language.empty())
        {
            String selected_language = user_settings.language.value_or(String());
            if (selected_language.empty())
            {
                selected_language = locale::GetSystemLanguage();
            }
            if (selected_language.empty())
            {
                selected_language = settings.default_language;
            }
            if (!selected_language.empty())
            {
                locale::SetLanguage(selected_language);
            }
        }

        if (!settings.input_action_map.empty())
        {
            const String input_action_map_path = project::ResolveProjectContentPath(content_root, settings.input_action_map);
            if (io::LoadActionMap(input_action_map_path))
            {
                backlog::Post("[Input] action map loaded: " + input_action_map_path);
            }
            else if (!io::IsFile(input_action_map_path))
            {
                backlog::Post("[Input] action map not found, skipping: " + input_action_map_path, backlog::LogLevel::Warning);
            }
            else
            {
                backlog::Post("[Input] failed to parse action map: " + input_action_map_path, backlog::LogLevel::Error);
            }
        }
        else
        {
            io::ClearActionMap();
        }

        if (!settings.game_data_schema.empty())
        {
            const String schema_path = project::ResolveProjectContentPath(content_root, settings.game_data_schema);
            const String save_file = DeriveGameDataSaveFile(settings.game_data_schema);
            if (game_data.LoadSchema(schema_path.c_str()))
            {
                backlog::Post("[GameData] schema loaded: " + schema_path);
            }
            else
            {
                backlog::Post("[GameData] failed to load schema: " + schema_path, backlog::LogLevel::Error);
            }
            if (game_data.Load(settings.project_name.c_str(), save_file.c_str()))
            {
                backlog::Post("[GameData] save loaded: " + save_file);
            }
            else
            {
                backlog::Post("[GameData] no save found, using defaults: " + save_file, backlog::LogLevel::Default);
            }
        }

        if (scene_manager)
        {
            scene_manager->SetProjectSettings(&settings);
        }
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

        backlog::Post("[Startup] WonEngine " + String(GetVersionString()));
        backlog::Post("[Startup] Project: " + project_settings.project_name);
        backlog::Post("[Startup] Startup scene: " + project_settings.startup_scene);
        backlog::Post("[Startup] Content root: " + project::GetContentRoot(project_settings));

        utils::Timer startup_timer;
        utils::Timer startup_phase_timer;
        auto log_startup_phase = [&startup_phase_timer](const char* phase)
        {
            wonlog("[Startup] %s: %.1f ms", phase, startup_phase_timer.ElapsedMilliSeconds());
            startup_phase_timer.Reset();
        };

        console::LoadConfig(project_settings.project_name);
        console::ApplyCommandLine(desc.command_line_args);

        reflection::RegisterBuiltinTypes();

        jobsystem::Initialize(desc.jobsystem_thread_count);
        won::physics::Initialize();
        log_startup_phase("jobsystem + physics");

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
        log_startup_phase("window");

        rendering::RHIDeviceDesc device_desc;
        device_desc.backend = project_settings.backend_type;
        device_desc.preference = desc.device_preference;
        device_desc.enable_debug_layer = true; // test

        device = rendering::CreateRHIDevice(device_desc);
        log_startup_phase("rhi device");

        rendering::RendererDesc renderer_desc;
        renderer_desc.device = device.get();
        if (!project_settings.project_root.empty())
        {
            renderer_desc.shader_bin_root_path = io::NormalizePath(io::CombinePath(project_settings.project_root, "CompiledShaders"));
        }
        else
        {
            renderer_desc.shader_bin_root_path = io::NormalizePath(io::CombinePath(io::GetExecutableDirectory(), "CompiledShaders"));
        }
        settings::LoadSettings(settings::GetUserSettingsPath(project_settings.project_name), user_settings);
		renderer_desc.vsync_enabled = user_settings.vsync.value_or(project_settings.vsync_enabled); // if user setting is not set, use project setting. note user setting is not changed
        renderer = rendering::CreateRenderer(renderer_desc);
        renderer->SetShadowResolutionScale(user_settings.shadow_resolution_scale.value_or(1.0f));
        log_startup_phase("renderer + shaders");

        if (device)
        {
			builtinfont::BuildAtlas(*device); // used for console rendering and debug text rendering
            log_startup_phase("builtin font atlas");
        }

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
        log_startup_phase("audio");

        scene_manager = std::make_unique<SceneManager>(&project_settings);

        script::ScriptRuntimeDesc script_desc = {};
        script_desc.game_data = &game_data;
        script_desc.audio_mixer = audio_mixer.get();
        script_desc.scene_manager = scene_manager.get();
        script_desc.content_root = project::GetContentRoot(project_settings);
        script_desc.user_settings = &user_settings;
        script_desc.project_settings = &project_settings;
        script_desc.apply_user_settings = [this]() { ApplyUserSettings(); };
        script_desc.save_user_settings = [this]() { return SaveUserSettings(); };
        script_runtime = script::CreateScriptRuntime(script_desc);
        if (script_runtime && !script_runtime->Initialize())
        {
            script_runtime.reset();
        }

        if (script_runtime)
        {
            script_runtime->SetViewResolver([this](float2 point) -> rendering::View*
            {
                for (Size i = views.size(); i-- > 0; )
                {
                    const rendering::Rect& vp = views[i]->viewport;
                    if (point.x >= vp.x && point.x < vp.x + vp.width &&
                        point.y >= vp.y && point.y < vp.y + vp.height)
                    {
                        return views[i].get();
                    }
                }
                return nullptr;
            });
        }

        log_startup_phase("script runtime");

        ApplyProjectSettings(project_settings);
        log_startup_phase("scene manager + project settings");
        wonlog("[Startup] initialize total: %.1f ms", startup_timer.ElapsedMilliSeconds());

        frame_timer.Reset();
        is_first_frame = true;
        is_running = true;
    }

    void Application::RebindViewCameras(ecs::Scene& scene)
    {
        for (const std::unique_ptr<rendering::View>& view_ptr : views)
        {
            if (view_ptr && view_ptr->scene == &scene)
            {
                view_ptr->camera_entity = view_ptr->FindSceneCamera();
            }
        }
    }

    void Application::ProcessSceneLifecycle()
    {
        scene_manager->FlushQueuedSceneLoads();

        for (ecs::Scene* activated : scene_manager->FlushCompletedSceneLoads())
        {
            RebindViewCameras(*activated);
        }
        scene_manager->FlushDeferredSceneRemovals();

        scene_manager->FlushPrefabSpawns();

        if (scene_manager->FlushPrefabPreloads())
        {
            rendering::utils::FlushEnqueuedResourceUploads(*device);
        }
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
            window->BringToForeground();
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
        ProcessSceneLifecycle();

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
        console::SaveConfig(project_settings.project_name);

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
        scene_manager.reset();

        if (audio_driver)
        {
            audio_driver->Stop();
            audio_driver.reset();
        }
        audio_mixer.reset();

        builtinfont::Shutdown();

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

#ifndef WON_SHIPPING
        console_overlay.Update();
        performance_overlay.Update(dt, device.get());
#endif
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

            view.UpdateUIInteraction();

            if (!simulation_paused && scene->GetUpdateIndex() != update_index)
            {
                scene->Update(dt);
            }
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

    SceneManager* Application::GetSceneManager()
    {
        return scene_manager.get();
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
#ifndef WON_SHIPPING
                    const int view_mode = r_debug_viewmode.GetInt();
                    if (view_mode >= 0 && view_mode < static_cast<int>(rendering::ViewMode::VIEWMODE_COUNT))
                    {
                        view_ptr->view_mode = static_cast<rendering::ViewMode>(view_mode);
                    }
                    if (r_debug_show_bvh.GetBool())
                    {
                        view_ptr->show_flags |= rendering::Show_BVH;
                    }
                    else
                    {
                        view_ptr->show_flags &= ~rendering::Show_BVH;
                    }
                    if (r_debug_show_ddgi.GetBool())
                    {
                        view_ptr->show_flags |= rendering::Show_DDGI;
                    }
                    else
                    {
                        view_ptr->show_flags &= ~rendering::Show_DDGI;
                    }
                    if (r_debug_show_colliders.GetBool())
                    {
                        view_ptr->show_flags |= rendering::Show_Colliders;
                    }
                    else
                    {
                        view_ptr->show_flags &= ~rendering::Show_Colliders;
                    }
                    view_ptr->freeze_culling = r_debug_freeze_culling.GetBool();
#endif

                    renderer->Render(*view_ptr);
                }
            }
        }
    }

    uint32 Application::AddView(rendering::View&& view)
    {
        views.push_back(std::make_unique<rendering::View>(std::move(view)));
        views.back()->options.aa_mode = user_settings.aa_mode.value_or(project_settings.aa_mode);
        views.back()->options.tonemap_mode = project_settings.tonemap_mode;
        return static_cast<uint32>(views.size() - 1);
    }

    void Application::ApplyUserSettings()
    {
        if (renderer)
        {
            renderer->SetVSync(user_settings.vsync.value_or(project_settings.vsync_enabled));
            renderer->SetShadowResolutionScale(user_settings.shadow_resolution_scale.value_or(1.0f));
        }

        const rendering::AntiAliasingMode effective_aa = user_settings.aa_mode.value_or(project_settings.aa_mode);
        for (std::unique_ptr<rendering::View>& view : views)
        {
            if (view)
            {
                view->options.aa_mode = effective_aa;
            }
        }
    }

    bool Application::SaveUserSettings()
    {
        return settings::SaveSettings(settings::GetUserSettingsPath(project_settings.project_name), user_settings);
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
#ifndef WON_SHIPPING
        if (renderer)
        {
            rendering::RHISubresourceBinding back_buffer_binding = {};
            if (renderer->GetCurrentBackBufferBinding(back_buffer_binding) && back_buffer_binding.resource)
            {
                const rendering::RHITextureDesc& desc = back_buffer_binding.resource->GetDesc().texture_desc;
                const float viewport_width = static_cast<float>(desc.width);
                const float viewport_height = static_cast<float>(desc.height);
                console_overlay.Draw(viewport_width, viewport_height);
                performance_overlay.Draw(viewport_width, viewport_height);
            }
            renderer->RenderDebug2D();
        }
#endif
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
