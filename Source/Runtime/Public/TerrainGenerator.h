#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "Mesh.h"
#include "TerrainComponent.h"

#include <memory>

namespace won::ecs
{
    struct TerrainHeightField
    {
        uint32 samples_x = 0;
        uint32 samples_z = 0;
        float cell_x = 0.0f;
        float cell_z = 0.0f;
        float offset_x = 0.0f;
        float offset_z = 0.0f;
        Vector<float> heights;
    };

    WONENGINE_API TerrainHeightField GenerateTerrainHeights(const TerrainComponent& terrain);
    // Authoring-time terrain mesh generator. Builds a CPU resource::Mesh (grid + height field) from a TerrainComponent recipe
    WONENGINE_API std::shared_ptr<resource::Mesh> GenerateTerrainMesh(const TerrainComponent& terrain);
}
