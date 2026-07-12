#pragma once
#include "Entity.h"
#include "JsonArchive.h"
#include "RuntimeExport.h"
#include "Types.h"

namespace won::ecs
{
    class Scene;
}

namespace won::serialize
{
    // v3: MaterialComponent serializes material_asset_path (shared ref / inline fork) instead of
    //     inline material_slots, and scenes carry a "materials" resource array.
    inline constexpr uint32 scene_format_version = 3;

    struct SaveSceneDesc
    {
        const Vector<ecs::Entity>* excluded_entities = nullptr;
    };

    WONENGINE_API void LoadScene(JsonArchive& archive, ecs::Scene& scene);
    WONENGINE_API void SaveScene(JsonArchive& archive, const ecs::Scene& scene, const SaveSceneDesc& desc = {});

	// can be used loading scene/prefab additively into an existing scene's preallocated root entity
    WONENGINE_API ecs::Entity LoadSceneAdditive(JsonArchive& archive, ecs::Scene& scene, ecs::Entity preallocated_root, Vector<ecs::Entity>& out_new_entities);
    WONENGINE_API bool SavePrefab(JsonArchive& archive, ecs::Scene& scene, ecs::Entity root);
}
