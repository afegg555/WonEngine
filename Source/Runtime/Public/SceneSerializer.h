#pragma once
#include "Entity.h"
#include "JsonArchive.h"
#include "ReflectionTypes.h"
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
    inline constexpr uint32 scene_format_version = 4;

    struct SaveSceneDesc
    {
        const Vector<ecs::Entity>* excluded_entities = nullptr;
    };

    WONENGINE_API void LoadScene(JsonArchive& archive, ecs::Scene& scene);
    WONENGINE_API void SaveScene(JsonArchive& archive, const ecs::Scene& scene, const SaveSceneDesc& desc = {});

	// can be used loading scene/prefab additively into an existing scene's preallocated root entity
    WONENGINE_API ecs::Entity LoadSceneAdditive(JsonArchive& archive, ecs::Scene& scene, Vector<ecs::Entity>& out_new_entities, ecs::Entity preallocated_root = ecs::INVALID_ENTITY);
    WONENGINE_API bool SavePrefab(JsonArchive& archive, ecs::Scene& scene, ecs::Entity root);


    // runtime only: entity refs are raw ids valid within the current scene session, never write these to a file
	WONENGINE_API bool SaveComponent(JsonArchive& archive, const ecs::Scene& scene, ecs::Entity entity, won::TypeId type_id); // dump a single component
    WONENGINE_API bool LoadComponent(JsonArchive& archive, ecs::Scene& scene, ecs::Entity entity, won::TypeId type_id);
	WONENGINE_API bool SaveEntitySnapshot(JsonArchive& archive, ecs::Scene& scene, ecs::Entity root); // dump a single entity and its subtree, including all components, to a snapshot
    WONENGINE_API ecs::Entity LoadEntitySnapshot(JsonArchive& archive, ecs::Scene& scene, Vector<ecs::Entity>& out_entities);
}
