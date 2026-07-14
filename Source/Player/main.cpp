#include "Application.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include "ProjectSettings.h"
#include "RenderingUtils.h"
#include "ResourceAsset.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "SplashWindow.h"

#include <algorithm>

int main(int argc, char** argv)
{
    // C:/GameProjects/MyGame/MyGame.wonproj
    // C:/GameProjects/MyGame
    // GetExecutableDirectory()

    won::ApplicationDesc app_desc = {};
    for (int i = 0; i < argc; ++i)
    {
        app_desc.command_line_args.emplace_back(argv[i]);
    }
    won::String project_settings_path;
    if (argc > 1)
    {
        project_settings_path = won::io::GetAbsolutePath(argv[1]);
    }
    else
    {
        // derive from executable name: ScriptedTriangle.exe -> ScriptedTriangle.wonproj
        const won::String exe_path = won::io::GetAbsolutePath(argc > 0 ? argv[0] : won::io::GetExecutableDirectory());
        const won::String exe_dir = won::io::IsFile(exe_path) ? won::io::GetDirectoryFromPath(exe_path) : exe_path;
        const won::String exe_name = won::io::GetFilename(exe_path);
        project_settings_path = won::io::CombinePath(exe_dir, won::io::ReplaceExtension(exe_name, won::project::project_file_extension));
        if (!won::io::IsFile(project_settings_path))
        {
            project_settings_path = won::io::CombinePath(exe_dir, won::project::default_project_file_name);
        }
    }
    if (!won::project::LoadSettings(project_settings_path, app_desc.project_settings))
    {
        return 1;
    }

    const won::String content_root = won::project::GetContentRoot(app_desc.project_settings);
    std::shared_ptr<won::platform::SplashWindow> splash = nullptr;
    if (app_desc.project_settings.splash_enabled)
    {
        won::platform::SplashWindowDesc splash_desc = {};
        splash_desc.title = app_desc.project_settings.splash_title.c_str();
        splash_desc.status = app_desc.project_settings.splash_status.c_str();
        won::String splash_image_path;
        if (!app_desc.project_settings.splash_image.empty())
        {
            splash_image_path = won::project::ResolveProjectContentPath(content_root, app_desc.project_settings.splash_image);
            splash_desc.image_path = splash_image_path.c_str();
        }
        splash = won::platform::CreateSplashWindow(splash_desc);
        if (splash)
        {
            splash->SetStatus("Starting renderer...");
        }
    }

    won::Application app;
    won::ApplicationDesc initialize_desc = app_desc;
    initialize_desc.defer_window_show = splash && app_desc.project_settings.window_visible;
    app.Initialize(initialize_desc);

    {
        if (splash)
        {
            splash->SetStatus("Loading startup scene...");
        }
        won::ecs::SceneDesc scene_desc = {};
        scene_desc.script_runtime = app.GetScriptRuntime();
        scene_desc.physics = won::project::GetPhysicsDesc(app_desc.project_settings);
        scene_desc.audio_mixer = app.GetAudioMixer();
        won::ecs::Scene& game_scene = app.GetSceneManager()->CreateScene(scene_desc);

        won::String startup_scene_path = app_desc.project_settings.startup_scene;
        if (!startup_scene_path.empty())
        {
            won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Read);
            const won::String normalized_startup_scene_path = won::project::ResolveProjectContentPath(content_root, startup_scene_path);
            if (archive.LoadFromFile(normalized_startup_scene_path))
            {
                won::serialize::LoadScene(archive, game_scene);
                if (archive.HasError())
                {
                    wonlog_warning("Startup scene load warning: %s (%s)", normalized_startup_scene_path.c_str(), archive.GetError().c_str());
                }
            }
            else
            {
                wonlog_warning("Failed to load startup scene: %s", normalized_startup_scene_path.c_str());
            }
        }

        if (splash)
        {
            splash->SetStatus("Loading scene resources...");
        }
        if (won::rendering::RHIDevice* device = app.GetDevice())
        {
            won::resource::LoadSceneResources(game_scene, content_root);
            won::rendering::utils::FlushEnqueuedResourceUploads(*device);
        }

        const float window_width = (std::max)(1.0f, static_cast<float>(app_desc.project_settings.window_width));
        const float window_height = (std::max)(1.0f, static_cast<float>(app_desc.project_settings.window_height));
        won::ecs::Entity camera_entity = won::ecs::INVALID_ENTITY;
        if (auto camera_array = game_scene.GetComponentArray<won::ecs::CameraComponent>())
        {
            for (won::Size i = 0; i < camera_array->GetSize(); ++i)
            {
                camera_array->data[i].SetAspectRatio(window_width / window_height);
                if (camera_entity == won::ecs::INVALID_ENTITY)
                {
                    camera_entity = camera_array->GetEntity(i);
                }
            }
        }

        won::rendering::View game_view = {};
        game_view.scene = &game_scene;
        game_view.camera_entity = camera_entity;
        game_view.viewport.width = app_desc.project_settings.window_width;
        game_view.viewport.height = app_desc.project_settings.window_height;
        game_view.scissor.width = app_desc.project_settings.window_width;
        game_view.scissor.height = app_desc.project_settings.window_height;
        app.AddView(game_view);

        if (initialize_desc.defer_window_show)
        {
            app.ShowMainWindow();
        }
        if (splash)
        {
            splash->Close();
        }

        while (app.IsRunning())
        {
            app.Run();
        }

        app.WaitIdle();
        app.ClearViews();
    }

    app.Shutdown();
    return 0;
}
