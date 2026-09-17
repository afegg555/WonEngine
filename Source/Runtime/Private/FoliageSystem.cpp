#include "FoliageSystem.h"

#include "Foliage.h"
#include "Scene.h"
#include "TerrainComponent.h"
#include "TerrainData.h"
#include "TransformComponent.h"

namespace won::ecs
{
    void FoliageSystem::Update(Scene& scene, float delta_time)
    {
        auto foliage_array = scene.GetComponentArray<FoliageComponent>().get();
        const uint32 foliage_count = static_cast<uint32>(foliage_array->GetSize());
        if (foliage_count == 0)
        {
            return;
        }

        const terrain::TerrainData* terrain_data = nullptr;
        float4x4 terrain_world = math::IDENTITY_MATRIX;
        auto terrain_array = scene.GetComponentArray<TerrainComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        for (uint32 i = 0; i < static_cast<uint32>(terrain_array->GetSize()); ++i)
        {
            const TerrainComponent& terrain = terrain_array->data[i];
            if (terrain.data && terrain.data->IsValid())
            {
                terrain_data = terrain.data.get();
                const Entity terrain_entity = terrain_array->index_to_entity[i];
                if (transform_array->HasData(terrain_entity))
                {
                    terrain_world = transform_array->GetData(terrain_entity).world_transform;
                }
                break;
            }
        }

        if (terrain_data)
        {
            for (uint32 i = 0; i < foliage_count; ++i)
            {
                FoliageComponent& foliage = foliage_array->data[i];
                if (foliage.IsActive() && foliage.IsDirty())
                {
                    foliage::ScatterFoliage(foliage, *terrain_data, terrain_world);
                }
            }
        }
    }
}
