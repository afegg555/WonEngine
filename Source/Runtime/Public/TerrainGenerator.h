#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "Mesh.h"
#include "TerrainComponent.h"

#include <memory>

namespace won::ecs
{
    // Authoring-time terrain mesh generator. Builds a CPU resource::Mesh (grid + height field) from a TerrainComponent recipe
    WONENGINE_API std::shared_ptr<resource::Mesh> GenerateTerrainMesh(const TerrainComponent& terrain);
}
