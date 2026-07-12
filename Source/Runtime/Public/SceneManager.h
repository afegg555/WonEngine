#pragma once
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

        void ReloadScene(ecs::Scene& scene, const String& path);
        void SpawnQueuedPrefabs(ecs::Scene& scene);
        void FlushPrefabSpawns();
        void PreloadPrefab(const String& path);

    private:
        Vector<std::unique_ptr<ecs::Scene>> scenes;
        const project::ProjectSettings* project_settings;
        UnorderedMap<String, Vector<std::shared_ptr<void>>> prefab_resource_cache;
    };
}
