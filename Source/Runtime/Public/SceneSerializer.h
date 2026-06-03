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
    inline constexpr uint32 scene_format_version = 1;

    struct SceneSerializeDesc
    {
        const Vector<ecs::Entity>* excluded_entities = nullptr;
    };

    WONENGINE_API void Serialize(JsonArchive& archive, ecs::Scene& scene, const SceneSerializeDesc& desc = {});
    WONENGINE_API void Serialize(JsonArchive& archive, const ecs::Scene& scene, const SceneSerializeDesc& desc = {});
}
