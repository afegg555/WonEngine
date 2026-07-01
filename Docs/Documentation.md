# Documentation

<p align="center">
  <img src="logo.png" width="360" alt="WonEngine Logo">
</p>

This document serves as a development reference for understanding and extending WonEngine's core systems.

> WonEngine is still in an early prototype stage.  
> The architecture, APIs, and documentation may change frequently as development continues.

## Source Layout

```text
Source/
  Runtime/        Core engine runtime source
  Editor/         Editor application source
  Shaders/        HLSL shader source and shader interop headers
  Plugins/        Runtime plugin source
  Tools/          Offline tool source, including ShaderOfflineCompiler
```

---

# Editor Guide

The editor is the main place to inspect scenes, adjust entities, tune rendering settings.

## Default Layout

The default editor layout contains these panels:

- `Viewport`: renders the active scene and handles scene picking.
- `Entity List`: lists scene entities and selects the current entity.
- `Inspector`: edits components on the selected entity.
- `Contents Browser`: reserved for project assets. This panel is still in progress.
- `Log`: displays messages posted through the engine backlog.
- `Profiler`: shows runtime performance and resource information when enabled.

Use `Window > Reset Layout` to restore the default docking layout.

## Viewport

The viewport displays the current scene through the editor camera. Left-click an object in the viewport to select it. The same selection is reflected in `Entity List` and edited in `Inspector`.

Camera movement currently uses the editor camera controls:

- `W`, `A`, `S`, `D`: move the camera.
- Right mouse drag: rotate the camera.
- Middle mouse drag: orbit around the focus point.
- `R`: reload the shader library during development.

The `Options` popup in the viewport contains rendering and debug toggles. `VSync` controls swapchain presentation. `BVH Debug` displays CPU BVH debug data. `DDGI Debug Overlay` shows DDGI volume, probe, and text debug information when DDGI is active in the scene.

## Entity List

`Entity List` is the main scene selection panel. Press `+` to create a new entity with a `TransformComponent` and `NameComponent`. Select an entity to inspect it. Right-click an entity to open the context menu and delete it.

The editor camera entity is protected from deletion and from removal of required camera components. This keeps the viewport usable while editing the scene.

## Inspector

`Inspector` edits the components attached to the selected entity. Each component section can be removed with the `X` button when removal is allowed. Use `Add Component` at the bottom of the inspector to attach supported components.

Common editing flow:

1. Create or select an entity.
2. Add the components needed for its role.
3. Edit component values in the inspector.
4. Use the viewport and debug panels to check the result.

## Component Editing

`NameComponent` controls the display name shown in the entity list.

`TransformComponent` edits position, rotation, and scale. These values drive camera, light, geometry, and hierarchy behavior.

`HierarchyComponent` stores a parent entity id.

`CameraComponent` controls projection mode, near and far planes, field of view, orthographic size, and exposure-related values.

`LightComponent` controls light type, color, intensity, range, cone angles, active state, dynamic state, shadows, and cascade settings.

`SkyComponent` controls the procedural sky, sun direction, sun color, sky colors, and ground colors.

`EnvironmentLightingComponent` controls ambient lighting and GI mode. `DDGI` mode uses the active `DDGIVolumeComponent`.

`DDGIVolumeComponent` controls probe counts, spacing, volume offset, update rate, hysteresis, bias values, and max trace distance. Use `Update Scene GPUBVH` after changing scene geometry that should participate in DDGI.

`GeometryComponent` shows whether a mesh is assigned and whether the entity casts shadows.

`MaterialComponent` edits material slots, shader type, render flags, base color, metallic, roughness, reflectance, anisotropy, sheen, clearcoat, and assigned texture slots.

## Logs And Profiling

`Log` collects engine messages. Use `Copy to Clipboard` when reporting issues, and `Clear` to reset the visible log buffer.

`Profiler` is disabled by default. Enable it only when measuring performance or resource usage because profiling can add overhead.

### Runtime Logs (Packaged Builds)

A packaged / standalone build writes a timestamped log file per run to:

```
%LOCALAPPDATA%\<ProjectName>\Logs\<YYYY-MM-DD_HH-MM-SS>.log
```

Each log opens with a `[Startup]` header — engine version, project name, startup scene, and content root — followed by subsystem init and scene-resource loading. Fatal conditions are tagged `[Error]`. When reporting a crash or startup failure, attach the most recent log from that folder.

## Current Editor Limits

Some workflows are still prototype-level. `Contents Browser` exists as a panel but does not yet provide a complete asset workflow. Scene save/load, editor-driven asset import, and richer prefab or project workflows should be documented here once those systems are exposed in the editor.

---

# C++ Runtime Guide

This section focuses on the public surface that game, tool, and plugin developers are expected to use when extending WonEngine in C++. Prefer headers under `Source/Runtime/Public` and plugin API headers under `Source/Plugins`. Code in `Source/Runtime/Private` is an implementation detail unless a feature explicitly requires engine-side changes.

## Application Entry Point

The easiest way to build an executable is to derive from `won::Application`, override the lifecycle hooks you need, and call `Initialize()` before entering the run loop.

```cpp
#include "Application.h"

class GameApplication : public won::Application
{
public:
    void Initialize(const won::ApplicationDesc& desc) override
    {
        won::Application::Initialize(desc);
        // Create scenes, load plugins, initialize game state.
    }

    void Update(float dt) override
    {
        won::Application::Update(dt);
        // Advance game logic.
    }

    void Shutdown() override
    {
        // Release game-owned resources before the renderer shuts down.
        won::Application::Shutdown();
    }
};

int main()
{
    won::ApplicationDesc desc;
    desc.window.title = "WonEngine Game";
    desc.window.width = 1280;
    desc.window.height = 720;
    desc.renderer_type = won::rendering::RendererType::Forward;
    desc.backend_type = won::rendering::RHIBackend::DirectX12;

    GameApplication app;
    app.Initialize(desc);
    while (app.IsRunning())
    {
        app.Run();
    }
    return 0;
}
```

Use `Initialize()` for long-lived setup, `Update(float dt)` for simulation, `RenderUI()` for custom UI, and `Shutdown()` for explicit release order. If you create GPU resources directly, keep ownership clear and release them before the base application shutdown path destroys the renderer and device.

## Scene And ECS Workflow

WonEngine uses an ECS-style scene. An entity is just an id; behavior comes from components and systems.

```cpp
won::ecs::Scene scene;

won::ecs::Entity entity = scene.CreateEntity();
scene.AddComponent<won::ecs::NameComponent>(entity)->value = "Player";

auto* transform = scene.AddComponent<won::ecs::TransformComponent>(entity);
transform->position = { 0.0f, 0.0f, 0.0f };
transform->SetDirty();

scene.Update(delta_time);
```

Use `Scene::CreateEntity()` and `Scene::DestroyEntity()` to manage entity lifetime. Use `AddComponent<T>()`, `GetComponent<T>()`, `RemoveComponent<T>()`, and `HasComponent<T>()` for component access. Components are stored by the scene, so do not keep raw component pointers across operations that can remove or recreate components.

## Core Components

`TransformComponent` stores local position, rotation, and scale, then systems update world-space data. Prefer `Translate()`, `RotateRollPitchYaw()`, `Scale()`, or `SetDirty()` after direct edits so dependent systems know the transform changed.

`NameComponent` is a lightweight debug/editor label. It should not be used as a persistent gameplay id.

`HierarchyComponent` stores a parent entity id. It is intended for transform hierarchy relationships, with `INVALID_ENTITY` meaning no parent.

`GeometryComponent` references a shared `resource::Mesh`. Call `SetMesh()` instead of assigning `mesh` directly when possible, because it refreshes local bounds and marks the component dirty.

`MaterialComponent` owns one or more `MaterialSlot` values. Add slots with `AddMaterialSlot()` and edit properties such as `base_color`, `metallic`, `roughness`, and texture bindings.

`CameraComponent` is driven by its `TransformComponent`. Edit projection settings through methods such as `SetAspectRatio()`, `SetNearFar()`, `SetFOV_Y()`, `SetOrtho()`, and `SetOrthoVerticalSize()`.

`LightComponent` is also transform-driven. Use `type`, `color`, `intensity`, `range`, and shadow settings to describe the light, and use `SetActive()`, `SetCastShadow()`, and `SetDynamic()` for flags.

`SkyComponent`, `EnvironmentLightingComponent`, and `DDGIVolumeComponent` describe environment rendering. A typical scene has one active environment entity with sky settings, ambient or DDGI mode, and optional DDGI volume configuration.

## Creating Renderable Entities

A renderable entity usually needs `TransformComponent`, `GeometryComponent`, and `MaterialComponent`. The renderer consumes prepared render data, so meshes created by code should upload their buffers through `won::rendering::utils::CreateRenderData()`.

```cpp
auto entity = scene.CreateEntity();
scene.AddComponent<won::ecs::NameComponent>(entity)->value = "Ground";

auto* transform = scene.AddComponent<won::ecs::TransformComponent>(entity);
transform->position = { 0.0f, -1.0f, 0.0f };
transform->scale = { 10.0f, 1.0f, 10.0f };
transform->SetDirty();

auto mesh = std::make_shared<won::resource::Mesh>();
mesh->positions = {
    { -1.0f, 0.0f, -1.0f },
    { -1.0f, 0.0f, 1.0f },
    { 1.0f, 0.0f, -1.0f },
    { 1.0f, 0.0f, 1.0f }
};
mesh->normals = {
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f },
    { 0.0f, 1.0f, 0.0f }
};
mesh->indices = { 0, 1, 2, 2, 1, 3 };

won::resource::Submesh submesh;
submesh.first_index = 0;
submesh.index_count = 6;
submesh.first_vertex = 0;
submesh.material_slot = 0;
submesh.local_bounds.min = { -1.0f, 0.0f, -1.0f };
submesh.local_bounds.max = { 1.0f, 0.0f, 1.0f };
mesh->submeshes.push_back(submesh);

won::rendering::utils::CreateRenderData(*device, *mesh);

auto* geometry = scene.AddComponent<won::ecs::GeometryComponent>(entity);
geometry->SetMesh(mesh);
geometry->SetCastShadow(true);

auto* material = scene.AddComponent<won::ecs::MaterialComponent>(entity);
auto& slot = material->AddMaterialSlot();
slot.base_color = { 0.7f, 0.8f, 0.7f, 1.0f };
slot.roughness = 0.5f;
```

Imported assets should generally go through the `AssetImporter` plugin. Manual mesh creation is useful for primitives, procedural geometry, debug drawing, and tests.

## Systems

Systems are updated by `Scene::Update()`. The default scene registers transform, camera, light, geometry, material, environment, and renderable update systems. Custom systems can be added with `Scene::AddSystem()`.

```cpp
class SpinSystem : public won::ecs::System
{
public:
    won::ecs::ComponentMask GetWriteMask() const override
    {
        return won::ecs::transform_component_mask;
    }

    void Update(won::ecs::Scene& scene, float delta_time) override
    {
        auto transforms = scene.GetComponentArray<won::ecs::TransformComponent>();
        if (!transforms)
        {
            return;
        }

        for (won::Size i = 0; i < transforms->GetSize(); ++i)
        {
            transforms->data[i].RotateRollPitchYaw({ 0.0f, delta_time, 0.0f });
        }
    }
};

scene.AddSystem(std::make_shared<SpinSystem>());
```

Declare read and write masks honestly. The scene uses those masks to order systems and run independent batches through the job system. If two systems write the same component type, they will be serialized in insertion order.

## Views And Picking

Rendering is driven through `won::rendering::View`. A view references a scene, a camera entity, and viewport/scissor rectangles. `View::Update()` calls `Scene::Update()`, and `View::RayCast()` converts screen coordinates into a scene ray query using the active camera.

```cpp
won::rendering::View view;
view.scene = &scene;
view.camera_entity = camera_entity;
view.viewport = { 0, 0, window_width, window_height };
view.scissor = view.viewport;

won::ecs::RayCastHit hit;
if (view.RayCast(mouse_position, hit))
{
    won::ecs::Entity picked = hit.entity;
}
```

Ray casting depends on camera matrices, transform bounds, and scene BVH data, so call scene or view update before relying on picking results.

## Loading Assets

Use `won::resource::LoadImageFile()` for image data when you need CPU-side pixels. The loader caches normalized paths and returns `std::shared_ptr<Image>`.

```cpp
auto image = won::resource::LoadImageFile("Contents/Images/env_comp.png", 4);
if (image && image->IsValid())
{
    // Create an RHI texture or keep the pixels for tool-side processing.
}
```

Use `AssetImporterAPI::Import()` to load supported mesh formats into a scene. The current importer path is focused on `.obj` and `.gltf` files.

```cpp
won::plugin::PluginManager plugins;
plugins.LoadPlugin(WON_IID_ASSET_IMPORTER);

auto importer = plugins.GetPlugin(WON_IID_ASSET_IMPORTER);
auto* api = static_cast<won::plugin::AssetImporterAPI*>(
    importer->QueryInterface(WON_IID_ASSET_IMPORTER, WON_VID_ASSET_IMPORTER));

won::ecs::Entity root_entity = won::ecs::INVALID_ENTITY;
api->Import(importer.get(), "Contents/Models/Obj/Sphere/sphere.obj", &scene, device.get(), root_entity);
```

Imported meshes create entities with transform, geometry, material, and name data where applicable. After import, you can edit components like any other scene entity.

## Plugins

Plugins expose a small C-style function table through `Plugin::QueryInterface()`. This keeps the runtime boundary simple and avoids sharing concrete plugin classes across module boundaries.

```cpp
won::plugin::PluginManager plugins;
if (plugins.LoadPlugin(WON_IID_CAMERA_CONTROLLER))
{
    auto plugin = plugins.GetPlugin(WON_IID_CAMERA_CONTROLLER);
    auto* api = static_cast<won::plugin::CameraControllerAPI*>(
        plugin->QueryInterface(WON_IID_CAMERA_CONTROLLER, WON_VID_CAMERA_CONTROLLER));
}
```

When writing a plugin, implement `Plugin`, return stable interface and version strings, expose a public API struct from the plugin header, and use `IMPLEMENT_PLUGIN(PluginClass, PluginName)` in the implementation file. Keep plugin-owned state inside the plugin object and pass engine data through explicit API parameters.

## Input And Window Access

Use the `won::io` namespace for keyboard and mouse state. Input is updated by the application platform layer, and game code should read state during `Update()`.

```cpp
if (won::io::IsPressed(won::io::KEYBOARD_BUTTON_SPACE))
{
    // Trigger one-shot action.
}

if (won::io::IsDown(won::io::Button('W')))
{
    // Continuous movement.
}

const won::io::MouseState& mouse = won::io::GetMouseState();
```

Use `platform::WindowDesc` through `ApplicationDesc` for normal window configuration. Access the native handle only for platform integrations such as ImGui or OS-specific tooling.

## Shaders

Runtime shaders live under `Source/Shaders`. Shared C++ and HLSL layout contracts belong in shader interop headers such as `ShaderInterop_Renderer.h`. Keep struct packing and enum values compatible on both sides when changing shader data.

Generated shader binaries are written to:

```text
CompiledShaders/
```

During editor development, shaders can be reloaded through `won::rendering::ReloadShaderLibrary(device)`. Offline compilation is handled by `Source/Tools/ShaderOfflineCompiler`.

## Debugging Utilities

Use `won::backlog::Post()` for engine/editor log messages. Use `won::profiler::ScopedRangeCPU` around meaningful CPU work that should appear in profiling output. Use `won::utils::Timer` for local timing when profiling integration is unnecessary.

Avoid depending on private renderer internals for debug views. Prefer public debug state exposed by `Renderer::SetDebugOptions()` and `Renderer::GetDebugState()`.

## Build Artifacts

Runtime binaries and executables are generated into:

```text
Binary/
```

Compiled shader binaries are generated into:

```text
CompiledShaders/
```

Runtime and editor assets are located in:

```text
Contents/
```

---

# Links

- [Features](Features.md)
