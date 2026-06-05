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

int main(int argc, char** argv)
{
    // C:/GameProjects/MyGame/MyGame.wonproj
    // C:/GameProjects/MyGame
    // GetExecutableDirectory()

    won::ApplicationDesc app_desc = {};
    const won::String project_path = won::io::GetAbsolutePath(argc > 1 ? argv[1] : won::io::GetExecutableDirectory());
    won::String project_settings_path = project_path;
    if (!won::io::IsFile(project_settings_path)) // if input arg is not .wonproj
    {
        project_settings_path = won::io::CombinePath(project_path, won::io::ReplaceExtension(won::io::GetFilename(project_path), won::project::project_file_extension)); // check if MyGame.wonproj exists
        if (!won::io::IsFile(project_settings_path))
        {
            project_settings_path = won::io::CombinePath(project_path, won::project::default_project_file_name); // default fallback to MyGame/Project.wonproj
        }
    }
    if (!won::project::LoadSettings(project_settings_path, app_desc.project_settings))
    {
        return 1;
    }

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

        won::String startup_scene_path = app_desc.project_settings.startup_scene;
        if (!startup_scene_path.empty())
        {
            if (won::utils::StartsWith(startup_scene_path, "/Contents/"))
            {
                startup_scene_path = won::io::CombinePath(content_root, startup_scene_path.substr(won::String("/Contents/").size()));
            }
            else if (!won::io::IsAbsolutePath(startup_scene_path))
            {
                startup_scene_path = won::io::CombinePath(content_root, startup_scene_path);
            }

            won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Read);
            if (archive.LoadFromFile(won::io::NormalizePath(startup_scene_path)))
            {
                won::serialize::Serialize(archive, game_scene);
            }
        }

        // reload resources

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
            if (auto material_array = game_scene.GetComponentArray<won::ecs::MaterialComponent>())
            {
                for (won::Size material_index = 0; material_index < material_array->GetSize(); ++material_index)
                {
                    won::ecs::MaterialComponent& material = material_array->data[material_index];
                    for (won::ecs::MaterialSlot& material_slot : material.material_slots)
                    {
                        for (won::uint32 texture_slot = 0; texture_slot < static_cast<won::uint32>(TEXTURESLOT_COUNT); ++texture_slot)
                        {
                            won::ecs::MaterialSlot::TextureMap& texture_map = material_slot.textures[texture_slot];
                            if (texture_map.texture_asset_path.empty())
                            {
                                continue;
                            }

                            won::String texture_path = texture_map.texture_asset_path;
                            if (won::utils::StartsWith(texture_path, "/Contents/"))
                            {
                                texture_path = won::io::CombinePath(content_root, texture_path.substr(won::String("/Contents/").size()));
                            }
                            else if (!won::io::IsAbsolutePath(texture_path))
                            {
                                texture_path = won::io::CombinePath(content_root, texture_path);
                            }
                            texture_path = won::io::NormalizePath(texture_path);

                            std::shared_ptr<won::resource::Image> image = nullptr;
                            if (won::utils::ToLower(won::io::GetExtension(texture_path)) == won::resource::texture_binary_extension)
                            {
                                image = won::resource::LoadTextureBinary(texture_path);
                            }
                            else
                            {
                                won::resource::AssetMeta meta = {};
                                if (won::resource::LoadAssetMeta(won::resource::GetAssetMetaPath(texture_path), meta) && !meta.binary_path.empty())
                                {
                                    won::String binary_path = meta.binary_path;
                                    if (won::utils::StartsWith(binary_path, "/Contents/"))
                                    {
                                        binary_path = won::io::CombinePath(content_root, binary_path.substr(won::String("/Contents/").size()));
                                    }
                                    else if (!won::io::IsAbsolutePath(binary_path))
                                    {
                                        binary_path = won::io::CombinePath(content_root, binary_path);
                                    }
                                    image = won::resource::LoadTextureBinary(won::io::NormalizePath(binary_path));
                                }
                                if (!image)
                                {
                                    image = won::resource::LoadImageFile(texture_path, 4);
                                }
                            }

                            const bool color_texture = texture_slot == BASECOLORMAP || texture_slot == EMISSIVEMAP || texture_slot == SHEENCOLORMAP;
                            const won::rendering::RHIFormat texture_format = color_texture ? won::rendering::RHIFormat::R8G8B8A8UnormSrgb : won::rendering::RHIFormat::R8G8B8A8Unorm;
                            if (image && image->IsValid() && won::rendering::utils::CreateRenderData(*device, *image, texture_format, true))
                            {
                                texture_map.texture = image->render_data.texture;
                                texture_map.res_handle = image->render_data.srv;
                            }
                        }
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
                    else if (!script_slot.script_path.empty() && !won::io::IsAbsolutePath(script_slot.script_path))
                    {
                        script_slot.script_path = won::io::CombinePath(content_root, script_slot.script_path);
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
