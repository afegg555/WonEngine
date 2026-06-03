#pragma once
#include "Entity.h"
#include "JsonArchive.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;
}

namespace won::serialize
{
    struct SceneSerializeDesc
    {
        const Vector<ecs::Entity>* excluded_entities = nullptr;
    };

    WONENGINE_API void Serialize(JsonArchive& archive, ecs::Scene& scene, const SceneSerializeDesc& desc = {});
    WONENGINE_API void Serialize(JsonArchive& archive, const ecs::Scene& scene, const SceneSerializeDesc& desc = {});
}
