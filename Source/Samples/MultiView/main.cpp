// Native sample: one scene, two views. Each view resolves its own camera through CameraComponent::viewer_index,
// and a per-view sequence drives its cinematic camera independently.

#include "Application.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "ProjectSettings.h"
#include "Scene.h"
#include "SceneManager.h"

#include <algorithm>

namespace
{
    void AddSplitView(won::Application& app, won::ecs::Scene& scene, const won::rendering::Rect& viewport, won::uint32 ui_layer_mask)
    {
        won::rendering::View view = {};
        view.scene = &scene;
        view.options.resize_policy = won::rendering::ViewResizePolicy::Manual;
        view.options.update_camera_aspect = false;
        view.viewport = viewport;
        view.scissor = viewport;
        view.ui_layer_mask = ui_layer_mask;
        app.AddView(std::move(view));
    }
}

int main(int argc, char** argv)
{
    won::ApplicationDesc app_desc = {};
    for (int i = 0; i < argc; ++i)
    {
        app_desc.command_line_args.emplace_back(argv[i]);
    }

    const won::String project_settings_path = won::io::CombinePath(won::io::NormalizePath(won::String(PROJECTS_ROOT_DIR)), "MultiView/MultiView.wonproj");
    if (!won::project::LoadSettings(project_settings_path, app_desc.project_settings))
    {
        wonlog_error("Failed to load project settings: %s", project_settings_path.c_str());
        return 1;
    }

    won::Application app;
    app.Initialize(app_desc);

    {
        won::ecs::SceneDesc scene_desc = {};
        scene_desc.script_runtime = app.GetScriptRuntime();
        scene_desc.physics = won::project::GetPhysicsDesc(app_desc.project_settings);
        scene_desc.audio_mixer = app.GetAudioMixer();

        won::ecs::Scene& scene = app.GetSceneManager()->CreateScene(scene_desc);
        won::String error;
        if (!app.GetSceneManager()->LoadSceneContents(scene, app_desc.project_settings.startup_scene, true, &error))
        {
            wonlog_error("Failed to load scene: %s", error.c_str());
            return 1;
        }

        const won::int32 window_width = (std::max)(2, app_desc.project_settings.window_width);
        const won::int32 window_height = (std::max)(1, app_desc.project_settings.window_height);
        const won::int32 split_width = window_width / 2;
        const won::int32 gutter = 2;

        AddSplitView(app, scene, { 0, 0, split_width - gutter, window_height }, 1);
        AddSplitView(app, scene, { split_width + gutter, 0, window_width - split_width - gutter, window_height }, 2);

        const float split_aspect = static_cast<float>(split_width) / static_cast<float>(window_height);
        if (auto camera_array = scene.GetComponentArray<won::ecs::CameraComponent>())
        {
            for (won::Size i = 0; i < camera_array->GetSize(); ++i)
            {
                camera_array->data[i].SetAspectRatio(split_aspect);
            }
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
