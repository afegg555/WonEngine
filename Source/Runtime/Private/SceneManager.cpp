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

    SceneManager::~SceneManager()
    {
        for (const std::unique_ptr<SceneLoadRequest>& request : scene_load_requests)
        {
            jobsystem::Wait(request->ctx);
        }
    }

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

        queued_scene_loads.erase(
            std::remove_if(queued_scene_loads.begin(), queued_scene_loads.end(),
                [scene](const QueuedSceneLoad& queued) { return queued.target == scene; }),
            queued_scene_loads.end());

        for (auto it = scene_load_requests.begin(); it != scene_load_requests.end();)
        {
            if ((*it)->target == scene)
            {
                jobsystem::Wait((*it)->ctx);
                it = scene_load_requests.erase(it);
            }
            else
            {
                ++it;
            }
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

    void SceneManager::QueueSceneLoad(ecs::Scene& target, const String& path)
    {
        for (QueuedSceneLoad& queued : queued_scene_loads)
        {
            if (queued.target == &target)
            {
                queued.path = path;
                return;
            }
        }
        queued_scene_loads.push_back({ &target, path });
    }

    bool SceneManager::IsLoading(const ecs::Scene* scene) const
    {
        for (const QueuedSceneLoad& queued : queued_scene_loads)
        {
            if (queued.target == scene)
            {
                return true;
            }
        }
        for (const std::unique_ptr<SceneLoadRequest>& request : scene_load_requests)
        {
            if (request->target == scene && !request->discarded)
            {
                return true;
            }
        }
        return false;
    }

    void SceneManager::FlushQueuedSceneLoads()
    {
        Vector<QueuedSceneLoad> queued;
        queued.swap(queued_scene_loads);
        for (const QueuedSceneLoad& load : queued)
        {
            AsyncSceneLoad(*load.target, load.path);
        }
    }

    bool SceneManager::LoadSceneContents(ecs::Scene& scene, const String& path, bool parallel, bool clear_entities, String* out_error)
    {
        const String content_root = project::GetContentRoot(*project_settings);
        const String full_path = project::ResolveProjectContentPath(content_root, path);
        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(full_path))
        {
            if (out_error)
            {
                *out_error = "failed to load archive: " + full_path;
            }
            return false;
        }

        if (clear_entities)
        {
            scene.ClearEntities();
        }
        serialize::LoadScene(archive, scene);
        if (archive.HasError() && out_error)
        {
            *out_error = archive.GetError();
        }
        resource::LoadSceneResources(scene, content_root, parallel);
        return true;
    }

    void SceneManager::ReloadScene(ecs::Scene& scene, const String& path)
    {
        String error;
        if (!LoadSceneContents(scene, path, true, true, &error))
        {
            backlog::Post("[SceneTransition] " + error, backlog::LogLevel::Error);
            return;
        }
        if (!error.empty())
        {
            backlog::Post("[SceneTransition] archive warning: " + error, backlog::LogLevel::Warning);
        }
        backlog::Post("[SceneTransition] complete: " + path, backlog::LogLevel::Default);
    }

    void SceneManager::AsyncSceneLoad(ecs::Scene& target, const String& path)
    {
        for (const std::unique_ptr<SceneLoadRequest>& existing : scene_load_requests)
        {
            if (existing->target == &target)
            {
                existing->discarded = true;
            }
        }

        auto request = std::make_unique<SceneLoadRequest>();
        request->target = &target;
        request->path = path;
        request->staging = std::make_unique<ecs::Scene>();
        for (const won::TypeDesc* type_desc : target.GetComponentTypes())
        {
            request->staging->RegisterComponent(type_desc);
        }

        SceneLoadRequest* job = request.get();

        jobsystem::Execute(job->ctx, [this, job](jobsystem::JobArgs)
        {
            String error;
            if (!LoadSceneContents(*job->staging, job->path, true, false, &error))
            {
                backlog::Post("[SceneTransition] " + error, backlog::LogLevel::Error);
                job->failed.store(true, std::memory_order_release);
                job->ready.store(true, std::memory_order_release);
                return;
            }
            backlog::Post("[SceneTransition] staged: " + job->path, backlog::LogLevel::Default);
            job->ready.store(true, std::memory_order_release);
        });

        scene_load_requests.push_back(std::move(request));
    }

    Vector<ecs::Scene*> SceneManager::FlushCompletedSceneLoads()
    {
        const bool allow_activate = !rendering::utils::HasPendingResourceUploads();

        Vector<ecs::Scene*> activated_scenes;
        for (auto it = scene_load_requests.begin(); it != scene_load_requests.end();)
        {
            SceneLoadRequest& request = **it;
            if (!request.ready.load(std::memory_order_acquire))
            {
                ++it;
                continue;
            }

            if (request.failed.load(std::memory_order_acquire) || request.discarded)
            {
                it = scene_load_requests.erase(it);
                continue;
            }

			if (!allow_activate) // wait until all resource uploads are complete before activating the scene
            {
                ++it;
                continue;
            }

            request.target->SwapContents(*request.staging);
            DeferredSceneRemoval removal = {};
            removal.frames_left = 8;
            removal.contents = std::move(request.staging);
            deferred_scene_removals.push_back(std::move(removal));
            activated_scenes.push_back(request.target);
            backlog::Post("[SceneTransition] complete: " + request.path, backlog::LogLevel::Default);
            it = scene_load_requests.erase(it);
        }
        return activated_scenes;
    }

    void SceneManager::FlushDeferredSceneRemovals()
    {
        for (auto it = deferred_scene_removals.begin(); it != deferred_scene_removals.end();)
        {
            if (it->frames_left > 0)
            {
                --it->frames_left;
            }
            if (it->frames_left == 0)
            {
                it = deferred_scene_removals.erase(it);
            }
            else
            {
                ++it;
            }
        }
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

    void SceneManager::QueuePrefabPreload(const String& path)
    {
        if (std::find(queued_prefab_preloads.begin(), queued_prefab_preloads.end(), path) != queued_prefab_preloads.end())
        {
            return;
        }
        queued_prefab_preloads.push_back(path);
    }

    bool SceneManager::FlushPrefabPreloads()
    {
        if (queued_prefab_preloads.empty())
        {
            return false;
        }
        Vector<String> queued;
        queued.swap(queued_prefab_preloads);
        for (const String& path : queued)
        {
            PreloadPrefab(path);
        }
        return true;
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
