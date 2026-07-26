// These are native C++ examples that use the engine API directly(Not using editor).
// PS: Use the editor for actual game work, unless you really miss wiring everything by hand.

#include "Application.h"
#include "RenderingUtils.h"
#include "Scene.h"

#include <algorithm>

int main()
{
    won::ApplicationDesc app_desc = {};
    app_desc.project_settings.project_name = "HelloTriangle";
    app_desc.project_settings.window_title = "Won Engine Hello Triangle";

    won::Application app;
    app.Initialize(app_desc);

    {
        won::ecs::Scene game_scene;
        won::ecs::Entity camera_entity = game_scene.CreateEntity();
        if (won::ecs::TransformComponent* camera_transform = game_scene.AddComponent<won::ecs::TransformComponent>(camera_entity))
        {
            camera_transform->position = { 0.0f, 0.0f, -3.0f };
            camera_transform->SetDirty();
        }
        if (won::ecs::CameraComponent* camera = game_scene.AddComponent<won::ecs::CameraComponent>(camera_entity))
        {
            float window_width = (std::max)(1.0f, static_cast<float>(app_desc.project_settings.window_width));
            float window_height = (std::max)(1.0f, static_cast<float>(app_desc.project_settings.window_height));
            camera->SetAspectRatio(window_width / window_height);
            camera->SetNearFar(0.1f, 1000.0f);
            camera->SetFOV_Y(won::math::PI / 3.0f);
        }
        if (won::ecs::NameComponent* name = game_scene.AddComponent<won::ecs::NameComponent>(camera_entity))
        {
            name->value = "Hello Triangle Camera";
        }

        won::ecs::Entity triangle_entity = game_scene.CreateEntity();
        game_scene.AddComponent<won::ecs::TransformComponent>(triangle_entity);
        std::shared_ptr<won::resource::Mesh> triangle_mesh = std::make_shared<won::resource::Mesh>();
        triangle_mesh->positions = {
            { -0.8f, -0.5f, 0.0f },
            { 0.0f, 0.8f, 0.0f },
            { 0.8f, -0.5f, 0.0f },
        };
        triangle_mesh->colors = {
            { 1.0f, 0.25f, 0.18f, 1.0f },
            { 0.18f, 0.85f, 0.35f, 1.0f },
            { 0.20f, 0.45f, 1.0f, 1.0f },
        };
        triangle_mesh->normals = {
            { 0.0f, 0.0f, -1.0f },
            { 0.0f, 0.0f, -1.0f },
            { 0.0f, 0.0f, -1.0f },
        };
        triangle_mesh->indices = { 0, 1, 2 };
        won::resource::Submesh triangle_submesh = {};
        triangle_submesh.first_index = 0;
        triangle_submesh.index_count = 3;
        triangle_submesh.first_vertex = 0;
        triangle_submesh.material_slot = 0;
        triangle_submesh.local_bounds.min = { -0.8f, -0.5f, 0.0f };
        triangle_submesh.local_bounds.max = { 0.8f, 0.8f, 0.0f };
        triangle_mesh->submeshes.push_back(triangle_submesh);
        if (won::rendering::RHIDevice* device = app.GetDevice())
        {
            won::rendering::utils::CreateRenderData(*device, *triangle_mesh);
        }
        if (won::ecs::GeometryComponent* geometry = game_scene.AddComponent<won::ecs::GeometryComponent>(triangle_entity))
        {
            geometry->SetMesh(triangle_mesh);
        }
        if (won::ecs::MaterialComponent* material = game_scene.AddComponent<won::ecs::MaterialComponent>(triangle_entity))
        {
            won::resource::MaterialSlot& material_slot = material->AddMaterialSlot();
            material_slot.material_type = won::resource::MaterialType::Unlit;
            material_slot.double_sided = true;
            material_slot.use_vertex_colors = true;
        }
        if (won::ecs::NameComponent* name = game_scene.AddComponent<won::ecs::NameComponent>(triangle_entity))
        {
            name->value = "Hello Triangle";
        }

        won::rendering::View game_view = {};
        game_view.scene = &game_scene;
        game_view.camera_entity = camera_entity;
        game_view.viewport.width = app_desc.project_settings.window_width;
        game_view.viewport.height = app_desc.project_settings.window_height;
        game_view.scissor.width = app_desc.project_settings.window_width;
        game_view.scissor.height = app_desc.project_settings.window_height;
        app.AddView(std::move(game_view));

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
