#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::nav
{
    inline constexpr float default_cell_size = 0.3f;
    inline constexpr float default_cell_height = 0.2f;
    inline constexpr float default_agent_radius = 0.5f;
    inline constexpr float default_agent_height = 2.0f;
    inline constexpr float default_agent_max_climb = 0.5f;
    inline constexpr float default_agent_max_slope = 45.0f;

    struct NavMeshBuildDesc
    {
		float cell_size = default_cell_size; // the width and depth of each voxel cell in world units
		float cell_height = default_cell_height; // the height of each voxel cell in world units
		float agent_radius = default_agent_radius; // the radius of the agent in world units
		float agent_height = default_agent_height; // the height of the agent in world units
		float agent_max_climb = default_agent_max_climb; // the maximum height the agent can climb in world units
		float agent_max_slope = default_agent_max_slope; // the maximum slope angle(degree) the agent can traverse
        bool use_bounds = false;
        float3 bounds_min = { 0.0f, 0.0f, 0.0f };
        float3 bounds_max = { 0.0f, 0.0f, 0.0f };
    };

    struct NavMeshImpl;

    class WONENGINE_API NavMesh
    {
    public:
        NavMesh();
        ~NavMesh();

        NavMesh(const NavMesh&) = delete;
        NavMesh& operator=(const NavMesh&) = delete;

        bool Build(const float3* positions, uint32 vertex_count, const uint32* indices, uint32 index_count, const NavMeshBuildDesc& desc = {});
        bool InitFromData(const uint8* data, uint32 size);
        const uint8* GetData() const;
        uint32 GetDataSize() const;
        void Clear();
        bool IsValid() const;

        bool FindPath(const float3& start, const float3& end, Vector<float3>& out_path) const;
        bool FindNearestPoint(const float3& position, float3& out_point) const;

    private:
        std::unique_ptr<NavMeshImpl> impl;
    };
}
