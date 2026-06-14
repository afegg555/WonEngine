#include "Application.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include "ProjectSettings.h"
#include "ResourceAsset.h"
#include "RenderingUtils.h"
#include "Scene.h"
#include "SceneSerializer.h"
#include "SplashWindow.h"
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
        won::ecs::Scene game_scene(scene_desc);

        won::String startup_scene_path = app_desc.project_settings.startup_scene;
        if (!startup_scene_path.empty())
        {
            won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Read);
            const won::String normalized_startup_scene_path = won::project::ResolveProjectContentPath(content_root, startup_scene_path);
            if (archive.LoadFromFile(normalized_startup_scene_path))
            {
                won::serialize::Serialize(archive, game_scene);
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

        // reload resources

        if (splash)
        {
            splash->SetStatus("Loading scene resources...");
        }
        if (won::rendering::RHIDevice* device = app.GetDevice())
        {
            if (auto geometry_array = game_scene.GetComponentArray<won::ecs::GeometryComponent>())
            {
                for (won::Size i = 0; i < geometry_array->GetSize(); ++i)
                {
                    won::ecs::GeometryComponent& geometry = geometry_array->data[i];
                    if (geometry.mesh_asset_path.empty())
                    {
                        continue;
                    }
                    const won::String mesh_path = won::project::ResolveProjectContentPath(content_root, geometry.mesh_asset_path);
                    won::String binary_path = mesh_path;
                    if (won::utils::ToLower(won::io::GetExtension(mesh_path)) != won::resource::mesh_binary_extension)
                    {
                        won::resource::AssetMeta meta = {};
                        if (won::resource::LoadAssetMeta(won::resource::GetAssetMetaPath(mesh_path), meta) && !meta.binary_path.empty())
                        {
                            binary_path = won::project::ResolveProjectContentPath(content_root, meta.binary_path);
                        }
                    }
                    std::shared_ptr<won::resource::Mesh> mesh = won::resource::LoadMeshBinary(binary_path);
                    if (mesh && won::rendering::utils::CreateRenderData(*device, *mesh))
                    {
                        geometry.SetMesh(mesh);
                    }
                    else
                    {
                        wonlog_warning("Failed to load scene mesh: %s", mesh_path.c_str());
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

                            const won::String texture_path = won::project::ResolveProjectContentPath(content_root, texture_map.texture_asset_path);

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
                                    const won::String binary_path = won::project::ResolveProjectContentPath(content_root, meta.binary_path);
                                    image = won::resource::LoadTextureBinary(binary_path);
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
                            else
                            {
                                wonlog_warning("Failed to load scene texture: %s", texture_path.c_str());
                            }
                        }
                    }
                }
            }

            if (auto text_array = game_scene.GetComponentArray<won::ecs::Text2DComponent>())
            {
                for (won::Size i = 0; i < text_array->GetSize(); ++i)
                {
                    won::ecs::Text2DComponent& text = text_array->data[i];
                    if (text.font_asset_path.empty())
                    {
                        continue;
                    }
                    const won::String font_path = won::project::ResolveProjectContentPath(content_root, text.font_asset_path);
                    text.font = won::resource::LoadFontFile(font_path);
                    if (text.font)
                    {
                        text.SetDirty();
                    }
                    else
                    {
                        wonlog_warning("Failed to load scene font: %s", font_path.c_str());
                    }
                }
            }

            if (auto text_array = game_scene.GetComponentArray<won::ecs::Text3DComponent>())
            {
                for (won::Size i = 0; i < text_array->GetSize(); ++i)
                {
                    won::ecs::Text3DComponent& text = text_array->data[i];
                    if (text.font_asset_path.empty())
                    {
                        continue;
                    }
                    const won::String font_path = won::project::ResolveProjectContentPath(content_root, text.font_asset_path);
                    text.font = won::resource::LoadFontFile(font_path);
                    if (text.font)
                    {
                        text.SetDirty();
                    }
                    else
                    {
                        wonlog_warning("Failed to load scene font: %s", font_path.c_str());
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
                    script_slot.script_path = won::project::ResolveProjectContentPath(content_root, script_slot.script_path);
                    if (!script_slot.script_path.empty() && !won::io::IsFile(script_slot.script_path))
                    {
                        wonlog_warning("Failed to find scene script: %s", script_slot.script_path.c_str());
                    }
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
