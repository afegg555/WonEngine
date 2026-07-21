#pragma once
#include "Types.h"

namespace won::ecs
{
    struct NavMeshComponent
    {
        float agent_radius = 0.5f;
        float agent_height = 2.0f;
        float agent_max_climb = 0.5f;
        float agent_max_slope = 45.0f;
        bool use_bounds = false;
        float3 bounds_center = { 0.0f, 0.0f, 0.0f };
        float3 bounds_extent = { 50.0f, 50.0f, 50.0f };
        uint32 include_layers = 0xFFFFFFFFu;
        String navmesh_asset_path;
    };
}
