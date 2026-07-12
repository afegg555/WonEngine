#include "SceneManager.h"

#include "Backlog.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include "ProjectSettings.h"
#include "ResourceAsset.h"
#include "Scene.h"
#include "SceneSerializer.h"

namespace won
{
    SceneManager::SceneManager(const project::ProjectSettings* project_settings)
        : project_settings(project_settings)
    {
    }

    SceneManager::~SceneManager() = default;

    void SceneManager::SetProjectSettings(const project::ProjectSettings* settings)
    {
        project_settings = settings;
    }

    ecs::Scene& SceneManager::CreateScene(const ecs::SceneDesc& desc)
    {
        scenes.push_back(std::make_unique<ecs::Scene>(desc));
        return *scenes.back();
    }

    void SceneManager::DestroyScene(ecs::Scene* scene)
    {
        if (!scene)
        {
            return;
        }

        for (auto it = scenes.begin(); it != scenes.end(); ++it)
        {
            if (it->get() == scene)
            {
                scenes.erase(it);
                return;
            }
        }
    }

    const Vector<std::unique_ptr<ecs::Scene>>& SceneManager::GetScenes() const
    {
        return scenes;
    }

    void SceneManager::ReloadScene(ecs::Scene& scene, const String& path)
    {
        const String content_root = project::GetContentRoot(*project_settings);
        const String full_path = io::CombinePath(content_root, path);
        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(full_path))
        {
            backlog::Post("[SceneTransition] failed to load archive: " + full_path, backlog::LogLevel::Error);
            return;
        }

        scene.ClearEntities();
        serialize::LoadScene(archive, scene);
        resource::LoadSceneResources(scene, content_root);
        backlog::Post("[SceneTransition] complete: " + full_path, backlog::LogLevel::Default);
    }

    void SceneManager::FlushPrefabSpawns()
    {
        for (const std::unique_ptr<ecs::Scene>& scene : scenes)
        {
            if (scene && !scene->GetPrefabSpawnQueue().empty())
            {
                SpawnQueuedPrefabs(*scene);
            }
        }
    }

    void SceneManager::SpawnQueuedPrefabs(ecs::Scene& scene)
    {
        const String content_root = project::GetContentRoot(*project_settings);
        const Vector<ecs::PrefabSpawnRequest> requests = scene.GetPrefabSpawnQueue();
        scene.ClearPrefabSpawnQueue();

        for (const ecs::PrefabSpawnRequest& request : requests)
        {
            const Vector<ecs::Entity>& entities = scene.GetEntities();
            if (std::find(entities.begin(), entities.end(), request.reserved_root) == entities.end())
            {
                continue;
            }

            serialize::JsonArchive archive(serialize::ArchiveMode::Read);
            if (!archive.LoadFromFile(io::CombinePath(content_root, request.path)))
            {
                continue;
            }
            Vector<ecs::Entity> new_entities;
            const ecs::Entity root = serialize::LoadSceneAdditive(archive, scene, request.reserved_root, new_entities);
            if (root == ecs::INVALID_ENTITY)
            {
                continue;
            }

            resource::LoadEntityResources(scene, content_root, new_entities);

            if (request.parent != ecs::INVALID_ENTITY)
            {
                ecs::HierarchyComponent* hierarchy = scene.GetComponent<ecs::HierarchyComponent>(root);
                if (!hierarchy)
                {
                    hierarchy = scene.AddComponent<ecs::HierarchyComponent>(root);
                }
                if (hierarchy)
                {
                    hierarchy->parent_id = request.parent;
                }
                scene.SetHierarchyTopologyDirty(true);
            }

            if (ecs::TransformComponent* transform = scene.GetComponent<ecs::TransformComponent>(root))
            {
                transform->position = request.position;
                if (request.yaw != 0.0f)
                {
                    transform->rotation = math::QuaternionFromYaw(request.yaw);
                }
                transform->SetDirty();
            }

            scene.SetBVHDirty();
        }
    }

    void SceneManager::PreloadPrefab(const String& path)
    {
        if (prefab_resource_cache.find(path) != prefab_resource_cache.end())
        {
            return;
        }

        const String content_root = project::GetContentRoot(*project_settings);
        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(io::CombinePath(content_root, path)))
        {
            return;
        }

        ecs::Scene scratch;
        Vector<ecs::Entity> temp_entities;
        const ecs::Entity root = serialize::LoadSceneAdditive(archive, scratch, ecs::INVALID_ENTITY, temp_entities);
        if (root == ecs::INVALID_ENTITY)
        {
            return;
        }

        resource::LoadEntityResources(scratch, content_root, temp_entities);

        Vector<std::shared_ptr<void>> resource_refs;
        for (ecs::Entity entity : temp_entities)
        {
            if (ecs::GeometryComponent* geometry = scratch.GetComponent<ecs::GeometryComponent>(entity); geometry && geometry->mesh)
            {
                resource_refs.push_back(geometry->mesh);
            }
            if (ecs::MaterialComponent* material = scratch.GetComponent<ecs::MaterialComponent>(entity); material && material->material)
            {
                resource_refs.push_back(material->material);
            }
        }

        prefab_resource_cache[path] = std::move(resource_refs);
    }
}
