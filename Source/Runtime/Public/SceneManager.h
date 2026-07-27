#pragma once
#include "Entity.h"
#include "JobSystem.h"
#include "RuntimeExport.h"
#include "Types.h"

namespace won::ecs
{
    class Scene;
    struct SceneDesc;
}

namespace won::project
{
    struct ProjectSettings;
}

namespace won
{
    class WONENGINE_API SceneManager
    {
    public:
        SceneManager(const project::ProjectSettings* project_settings);
        ~SceneManager();

        void SetProjectSettings(const project::ProjectSettings* project_settings);

        ecs::Scene& CreateScene(const ecs::SceneDesc& desc);
        void DestroyScene(ecs::Scene* scene);
        const Vector<std::unique_ptr<ecs::Scene>>& GetScenes() const;

        bool LoadSceneContents(ecs::Scene& scene, const String& path, bool parallel, String* out_error);
        void ReloadScene(ecs::Scene& scene, const String& path);
        void QueueSceneLoad(ecs::Scene& target, const String& path);
        bool IsLoading(const ecs::Scene* scene) const;
        void FlushQueuedSceneLoads();
        void AsyncSceneLoad(ecs::Scene& target, const String& path);
        Vector<ecs::Scene*> FlushCompletedSceneLoads();
        void FlushDeferredSceneRemovals();

        ecs::Entity LoadSceneAdditive(ecs::Scene& scene, const String& path, String* out_error);
        bool UnloadSceneAdditive(ecs::Scene& scene, ecs::Entity root);

        void SpawnQueuedPrefabs(ecs::Scene& scene);
        void FlushPrefabSpawns();
        void QueuePrefabPreload(const String& path);
        bool FlushPrefabPreloads();
        void PreloadPrefab(const String& path);

    private:
        struct QueuedSceneLoad
        {
            ecs::Scene* target = nullptr;
            String path;
        };

        struct SceneLoadRequest
        {
            ecs::Scene* target = nullptr;
            String path;
            std::unique_ptr<ecs::Scene> staging;
            std::atomic<bool> ready{ false };
            std::atomic<bool> failed{ false };
            bool discarded = false;
            jobsystem::Context ctx;
        };

        struct DeferredSceneRemoval
        {
            uint32 frames_left = 0;
            std::unique_ptr<ecs::Scene> contents;
        };

        Vector<std::unique_ptr<ecs::Scene>> scenes;
        Vector<QueuedSceneLoad> queued_scene_loads;
        Vector<String> queued_prefab_preloads;
        Vector<std::unique_ptr<SceneLoadRequest>> scene_load_requests;
        Vector<DeferredSceneRemoval> deferred_scene_removals;
        const project::ProjectSettings* project_settings;
        UnorderedMap<String, Vector<std::shared_ptr<void>>> prefab_resource_cache;
    };
}
