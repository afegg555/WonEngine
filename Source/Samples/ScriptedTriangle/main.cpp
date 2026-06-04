#include "Application.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include "ProjectSettings.h"
#include "ResourceAsset.h"
#include "RenderingUtils.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "StringUtils.h"

#include <algorithm>

int main()
{
    constexpr const char* project_settings_file_name = "ScriptedTriangleProjectSettings.json";
    constexpr const char* startup_scene_path = "Scenes/ScriptedTriangle.wonscene";

    won::ApplicationDesc app_desc = {};
    app_desc.project_settings.project_name = "ScriptedTriangle";
    app_desc.project_settings.window_title = "Won Engine Scripted Triangle";
    app_desc.project_settings.startup_scene = startup_scene_path;
    app_desc.project_settings.script_root = "Scripts";
#ifdef CONTENTS_ROOT_DIR
    won::String development_content_root = CONTENTS_ROOT_DIR;
    if (won::io::IsDirectory(development_content_root))
    {
        app_desc.project_settings.project_root = won::io::CombinePath(development_content_root, "..");
    }
#endif
    won::String project_settings_path = won::io::CombinePath(won::io::GetExecutableDirectory(), project_settings_file_name);
    won::project::LoadSettings(project_settings_path, app_desc.project_settings);

    won::String content_root = app_desc.project_settings.content_root.empty() ? "Contents" : app_desc.project_settings.content_root;
    if (!won::io::IsAbsolutePath(content_root))
    {
        content_root = won::io::CombinePath(app_desc.project_settings.project_root, content_root);
    }
    content_root = won::io::NormalizePath(content_root);

    won::Application app;
    app.Initialize(app_desc);

    {
        won::ecs::SceneDesc scene_desc = {};
        scene_desc.script_runtime = app.GetScriptRuntime();
        won::ecs::Scene game_scene(scene_desc);

        won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Read);
        if (archive.LoadFromFile(won::io::CombinePath(content_root, app_desc.project_settings.startup_scene)))
        {
            won::serialize::Serialize(archive, game_scene);
        }

        if (won::rendering::RHIDevice* device = app.GetDevice())
        {
            if (auto geometry_array = game_scene.GetComponentArray<won::ecs::GeometryComponent>())
            {
                for (won::Size i = 0; i < geometry_array->GetSize(); ++i)
                {
                    won::ecs::GeometryComponent& geometry = geometry_array->data[i];
                    won::String mesh_path = geometry.mesh_asset_path;
                    if (won::utils::StartsWith(mesh_path, "/Contents/"))
                    {
                        mesh_path = mesh_path.substr(won::String("/Contents/").size());
                    }
                    if (!won::io::IsAbsolutePath(mesh_path))
                    {
                        mesh_path = won::io::CombinePath(content_root, mesh_path);
                    }
                    std::shared_ptr<won::resource::Mesh> mesh = won::resource::LoadMeshBinary(won::io::NormalizePath(mesh_path));
                    if (mesh && won::rendering::utils::CreateRenderData(*device, *mesh))
                    {
                        geometry.SetMesh(mesh);
                    }
                }
            }
        }

        if (auto script_array = game_scene.GetComponentArray<won::ecs::ScriptComponent>())
        {
            for (won::Size i = 0; i < script_array->GetSize(); ++i)
            {
                for (won::ecs::ScriptSlot& script_slot : script_array->data[i].scripts)
                {
                    if (won::utils::StartsWith(script_slot.script_path, "/Contents/"))
                    {
                        script_slot.script_path = won::io::CombinePath(content_root, script_slot.script_path.substr(won::String("/Contents/").size()));
                    }
                    script_slot.script_path = won::io::NormalizePath(script_slot.script_path);
                }
            }
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
