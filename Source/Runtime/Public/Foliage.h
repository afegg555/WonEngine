#pragma once
#include "FoliageComponent.h"
#include "RuntimeExport.h"

namespace won::terrain
{
    struct TerrainData;
}

namespace won::foliage
{
    WONENGINE_API void ScatterFoliage(ecs::FoliageComponent& foliage, const terrain::TerrainData& terrain, const float4x4& terrain_world);
}
