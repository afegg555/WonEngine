#pragma once
#include "Types.h"
#include "TerrainData.h"

#include <memory>

namespace won::ecs
{
    struct TerrainComponent
    {
        std::shared_ptr<terrain::TerrainData> data;
        String terrain_data_path;

        void SetData(const std::shared_ptr<terrain::TerrainData>& value)
        {
            data = value;
        }
    };
}
