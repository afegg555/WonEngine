#include "FoliageSystem.h"

#include "Foliage.h"
#include "Scene.h"
#include "TerrainComponent.h"
#include "TerrainData.h"
#include "TransformComponent.h"
#include "DebugDraw.h"
#include "Console.h"
#include "MathUtils.h"

namespace won::ecs
{
    namespace
    {
        static console::ConsoleVariable r_foliage_enabled("r.foliage.enabled", true, "enable foliage scatter and rendering");
        static console::ConsoleVariable r_foliage_show("r.foliage.show", false, "draw foliage scatter points as debug crosses");

        constexpr uint32 max_overlay_crosses = 20000;
    }

    void FoliageSystem::Update(Scene& scene, float delta_time)
    {
        if (!r_foliage_enabled.GetBool())
        {
            return;
        }

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

        if (r_foliage_show.GetBool())
        {
            const uint32 color = debugdraw::PackRGBA8(0x40, 0xff, 0x40);
            uint32 drawn = 0;
            for (uint32 i = 0; i < foliage_count && drawn < max_overlay_crosses; ++i)
            {
                const FoliageComponent& foliage = foliage_array->data[i];
                for (const FoliageType& type : foliage.types)
                {
                    for (const FoliageInstance& instance : type.instances)
                    {
                        if (drawn >= max_overlay_crosses)
                        {
                            break;
                        }
                        debugdraw::Cross3D(instance.position, 0.25f * instance.scale, color);
                        ++drawn;
                    }
                }
            }
        }
    }
}
