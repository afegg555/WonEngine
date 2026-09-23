#include "FoliageSystem.h"

#include "Foliage.h"
#include "Scene.h"
#include "TerrainComponent.h"
#include "TerrainData.h"
#include "TransformComponent.h"
#include "PhysicsWorld.h"
#include "CollisionLayerComponent.h"

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
        auto collision_layer_array = scene.GetComponentArray<CollisionLayerComponent>().get();
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

        physics::PhysicsWorld* physics_world = scene.GetPhysicsWorld();
        for (uint32 i = 0; i < foliage_count; ++i)
        {
            FoliageComponent& foliage = foliage_array->data[i];
            const Entity entity = foliage_array->index_to_entity[i];
            if (!foliage.IsActive())
            {
                if (foliage.IsDirty())
                {
                    physics_world->RemoveFoliage(entity);
                    foliage.SetDirty(false);
                    scene.MarkGpuDirty(foliage_component_mask);
                }
                continue;
            }
            if (!foliage.IsDirty() || !terrain_data)
            {
                continue;
            }

            foliage::ScatterFoliage(foliage, *terrain_data, terrain_world);
            scene.MarkGpuDirty(foliage_component_mask);
            physics_world->RemoveFoliage(entity);
            const uint32 layer = collision_layer_array && collision_layer_array->HasData(entity)
                ? collision_layer_array->GetData(entity).layer : 0;
            physics_world->AddFoliage(entity, foliage, layer);
        }
    }
}
