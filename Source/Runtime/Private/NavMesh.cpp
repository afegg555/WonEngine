#include "NavMesh.h"
#include "Backlog.h"

#include <Recast.h>
#include <DetourAlloc.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>

#include <cstring>

namespace won::nav
{
    namespace
    {
        constexpr int max_path_polys = 256;
        constexpr int max_straight_points = 256;
        constexpr float nearest_extent_xz = 5.0f;
        constexpr float nearest_extent_y = 50.0f;
    }

    struct NavMeshImpl
    {
        dtNavMesh* nav_mesh = nullptr;
        dtNavMeshQuery* nav_query = nullptr;
        dtQueryFilter filter;
        unsigned char* nav_data = nullptr;
        int nav_data_size = 0;

        ~NavMeshImpl()
        {
            Release();
        }

        void Release()
        {
            if (nav_query)
            {
                dtFreeNavMeshQuery(nav_query);
                nav_query = nullptr;
            }
            if (nav_mesh)
            {
                dtFreeNavMesh(nav_mesh);
                nav_mesh = nullptr;
            }
            if (nav_data)
            {
                dtFree(nav_data);
                nav_data = nullptr;
                nav_data_size = 0;
            }
        }

        bool InitFromOwnedData(unsigned char* data, int size)
        {
            Release();
            nav_data = data;
            nav_data_size = size;
            nav_mesh = dtAllocNavMesh();
            if (!nav_mesh || dtStatusFailed(nav_mesh->init(data, size, 0)))
            {
                Release();
                return false;
            }
            nav_query = dtAllocNavMeshQuery();
            if (!nav_query || dtStatusFailed(nav_query->init(nav_mesh, 2048)))
            {
                Release();
                return false;
            }
            return true;
        }
    };

    NavMesh::NavMesh()
        : impl(std::make_unique<NavMeshImpl>())
    {
    }

    NavMesh::~NavMesh() = default;

    void NavMesh::Clear()
    {
        impl->Release();
    }

    bool NavMesh::IsValid() const
    {
        return impl->nav_mesh != nullptr && impl->nav_query != nullptr;
    }

    bool NavMesh::Build(const float3* positions, uint32 vertex_count, const uint32* indices, uint32 index_count, const NavMeshBuildDesc& desc)
    {
        impl->Release();

        if (!positions || vertex_count == 0 || !indices || index_count < 3)
        {
            backlog::Post("[NavMesh] build skipped: no input geometry", backlog::LogLevel::Warning);
            return false;
        }

        float bounds_min[3] = { positions[0].x, positions[0].y, positions[0].z };
        float bounds_max[3] = { positions[0].x, positions[0].y, positions[0].z };
        for (uint32 i = 1; i < vertex_count; ++i)
        {
            bounds_min[0] = (std::min)(bounds_min[0], positions[i].x);
            bounds_min[1] = (std::min)(bounds_min[1], positions[i].y);
            bounds_min[2] = (std::min)(bounds_min[2], positions[i].z);
            bounds_max[0] = (std::max)(bounds_max[0], positions[i].x);
            bounds_max[1] = (std::max)(bounds_max[1], positions[i].y);
            bounds_max[2] = (std::max)(bounds_max[2], positions[i].z);
        }

        if (desc.use_bounds)
        {
            bounds_min[0] = desc.bounds_min.x;
            bounds_min[1] = desc.bounds_min.y;
            bounds_min[2] = desc.bounds_min.z;
            bounds_max[0] = desc.bounds_max.x;
            bounds_max[1] = desc.bounds_max.y;
            bounds_max[2] = desc.bounds_max.z;
        }

        rcConfig config = {};
        config.cs = desc.cell_size;
        config.ch = desc.cell_height;
        config.walkableSlopeAngle = desc.agent_max_slope;
		config.walkableHeight = static_cast<int>(ceilf(desc.agent_height / config.ch)); // number of cells
        config.walkableClimb = static_cast<int>(floorf(desc.agent_max_climb / config.ch));
        config.walkableRadius = static_cast<int>(ceilf(desc.agent_radius / config.cs));
        config.maxEdgeLen = static_cast<int>(12.0f / config.cs);
        config.maxSimplificationError = 1.3f;
        config.minRegionArea = 8 * 8;
        config.mergeRegionArea = 20 * 20;
        config.maxVertsPerPoly = 6;
        config.detailSampleDist = config.cs * 6.0f;
        config.detailSampleMaxError = config.ch * 1.0f;
        rcVcopy(config.bmin, bounds_min);
        rcVcopy(config.bmax, bounds_max);
        rcCalcGridSize(config.bmin, config.bmax, config.cs, &config.width, &config.height);

        rcContext context;

		// Heightfield : width(x) * depth(z) cells, each cell contains a linked list of spans
		// each span's height may be different
		// no span for empty space, so the heightfield is a sparse representation of the geometry
        rcHeightfield* heightfield = rcAllocHeightfield();
        if (!heightfield || !rcCreateHeightfield(&context, *heightfield, config.width, config.height, config.bmin, config.bmax, config.cs, config.ch))
        {
            backlog::Post("[NavMesh] build failed: heightfield allocation", backlog::LogLevel::Warning);
            rcFreeHeightField(heightfield);
            return false;
        }

        const int triangle_count = static_cast<int>(index_count / 3);
        Vector<int> triangle_indices(index_count);
        for (uint32 i = 0; i < index_count; ++i)
        {
            triangle_indices[i] = static_cast<int>(indices[i]);
        }
        Vector<unsigned char> triangle_areas(triangle_count, 0);
        rcMarkWalkableTriangles(&context, config.walkableSlopeAngle, &positions[0].x, static_cast<int>(vertex_count), triangle_indices.data(), triangle_count, triangle_areas.data());
        if (!rcRasterizeTriangles(&context, &positions[0].x, static_cast<int>(vertex_count), triangle_indices.data(), triangle_areas.data(), triangle_count, *heightfield, config.walkableClimb))
        {
            backlog::Post("[NavMesh] build failed: rasterize", backlog::LogLevel::Warning);
            rcFreeHeightField(heightfield);
            return false;
        }

        rcFilterLowHangingWalkableObstacles(&context, config.walkableClimb, *heightfield);
        rcFilterLedgeSpans(&context, config.walkableHeight, config.walkableClimb, *heightfield);
        rcFilterWalkableLowHeightSpans(&context, config.walkableHeight, *heightfield);

        rcCompactHeightfield* compact = rcAllocCompactHeightfield();
        if (!compact || !rcBuildCompactHeightfield(&context, config.walkableHeight, config.walkableClimb, *heightfield, *compact))
        {
            backlog::Post("[NavMesh] build failed: compact heightfield", backlog::LogLevel::Warning);
            rcFreeCompactHeightfield(compact);
            rcFreeHeightField(heightfield);
            return false;
        }
        rcFreeHeightField(heightfield);

        if (!rcErodeWalkableArea(&context, config.walkableRadius, *compact))
        {
            backlog::Post("[NavMesh] build failed: erode", backlog::LogLevel::Warning);
            rcFreeCompactHeightfield(compact);
            return false;
        }
        if (!rcBuildDistanceField(&context, *compact) || !rcBuildRegions(&context, *compact, 0, config.minRegionArea, config.mergeRegionArea))
        {
            backlog::Post("[NavMesh] build failed: regions", backlog::LogLevel::Warning);
            rcFreeCompactHeightfield(compact);
            return false;
        }

        rcContourSet* contours = rcAllocContourSet();
        if (!contours || !rcBuildContours(&context, *compact, config.maxSimplificationError, config.maxEdgeLen, *contours))
        {
            backlog::Post("[NavMesh] build failed: contours", backlog::LogLevel::Warning);
            rcFreeContourSet(contours);
            rcFreeCompactHeightfield(compact);
            return false;
        }

        rcPolyMesh* poly_mesh = rcAllocPolyMesh();
        if (!poly_mesh || !rcBuildPolyMesh(&context, *contours, config.maxVertsPerPoly, *poly_mesh))
        {
            backlog::Post("[NavMesh] build failed: poly mesh", backlog::LogLevel::Warning);
            rcFreePolyMesh(poly_mesh);
            rcFreeContourSet(contours);
            rcFreeCompactHeightfield(compact);
            return false;
        }

        rcPolyMeshDetail* detail_mesh = rcAllocPolyMeshDetail();
        if (!detail_mesh || !rcBuildPolyMeshDetail(&context, *poly_mesh, *compact, config.detailSampleDist, config.detailSampleMaxError, *detail_mesh))
        {
            backlog::Post("[NavMesh] build failed: detail mesh", backlog::LogLevel::Warning);
            rcFreePolyMeshDetail(detail_mesh);
            rcFreePolyMesh(poly_mesh);
            rcFreeContourSet(contours);
            rcFreeCompactHeightfield(compact);
            return false;
        }
        rcFreeContourSet(contours);
        rcFreeCompactHeightfield(compact);

        if (poly_mesh->npolys == 0)
        {
            backlog::Post("[NavMesh] build produced 0 polygons - check cell_size/agent params vs scene scale", backlog::LogLevel::Warning);
            rcFreePolyMeshDetail(detail_mesh);
            rcFreePolyMesh(poly_mesh);
            return false;
        }

        for (int i = 0; i < poly_mesh->npolys; ++i)
        {
            if (poly_mesh->areas[i] == RC_WALKABLE_AREA)
            {
                poly_mesh->flags[i] = 1;
            }
        }

        dtNavMeshCreateParams params = {};
        params.verts = poly_mesh->verts;
        params.vertCount = poly_mesh->nverts;
        params.polys = poly_mesh->polys;
        params.polyAreas = poly_mesh->areas;
        params.polyFlags = poly_mesh->flags;
        params.polyCount = poly_mesh->npolys;
        params.nvp = poly_mesh->nvp;
        params.detailMeshes = detail_mesh->meshes;
        params.detailVerts = detail_mesh->verts;
        params.detailVertsCount = detail_mesh->nverts;
        params.detailTris = detail_mesh->tris;
        params.detailTriCount = detail_mesh->ntris;
        params.walkableHeight = desc.agent_height;
        params.walkableRadius = desc.agent_radius;
        params.walkableClimb = desc.agent_max_climb;
        rcVcopy(params.bmin, poly_mesh->bmin);
        rcVcopy(params.bmax, poly_mesh->bmax);
        params.cs = config.cs;
        params.ch = config.ch;
        params.buildBvTree = true;

        unsigned char* nav_data = nullptr;
        int nav_data_size = 0;
        if (!dtCreateNavMeshData(&params, &nav_data, &nav_data_size))
        {
            backlog::Post("[NavMesh] build failed: dtCreateNavMeshData", backlog::LogLevel::Warning);
            rcFreePolyMeshDetail(detail_mesh);
            rcFreePolyMesh(poly_mesh);
            return false;
        }

        const int poly_count = poly_mesh->npolys;
        const int vert_count = poly_mesh->nverts;
        rcFreePolyMeshDetail(detail_mesh);
        rcFreePolyMesh(poly_mesh);

        if (!impl->InitFromOwnedData(nav_data, nav_data_size))
        {
            backlog::Post("[NavMesh] build failed: dtNavMesh init", backlog::LogLevel::Warning);
            return false;
        }

        backlog::Post("[NavMesh] built: " + std::to_string(poly_count) + " polys, " + std::to_string(vert_count) + " verts", backlog::LogLevel::Default);
        return true;
    }

    bool NavMesh::InitFromData(const uint8* data, uint32 size)
    {
        if (!data || size == 0)
        {
            return false;
        }
        unsigned char* owned = static_cast<unsigned char*>(dtAlloc(static_cast<int>(size), DT_ALLOC_PERM));
        if (!owned)
        {
            return false;
        }
        std::memcpy(owned, data, size);
        return impl->InitFromOwnedData(owned, static_cast<int>(size));
    }

    const uint8* NavMesh::GetData() const
    {
        return impl->nav_data;
    }

    uint32 NavMesh::GetDataSize() const
    {
        return impl->nav_data_size > 0 ? static_cast<uint32>(impl->nav_data_size) : 0;
    }

    bool NavMesh::FindNearestPoint(const float3& position, float3& out_point) const
    {
        if (!IsValid())
        {
            return false;
        }
        const float extents[3] = { nearest_extent_xz, nearest_extent_y, nearest_extent_xz };
        dtPolyRef poly_ref = 0;
        float nearest[3] = {};
        if (dtStatusFailed(impl->nav_query->findNearestPoly(&position.x, extents, &impl->filter, &poly_ref, nearest)) || poly_ref == 0)
        {
            return false;
        }
        out_point = { nearest[0], nearest[1], nearest[2] };
        return true;
    }

    bool NavMesh::FindPath(const float3& start, const float3& end, Vector<float3>& out_path) const
    {
        out_path.clear();
        if (!IsValid())
        {
            return false;
        }

        const float extents[3] = { nearest_extent_xz, nearest_extent_y, nearest_extent_xz };
        dtPolyRef start_ref = 0;
        dtPolyRef end_ref = 0;
        float start_pos[3] = {};
        float end_pos[3] = {};
        if (dtStatusFailed(impl->nav_query->findNearestPoly(&start.x, extents, &impl->filter, &start_ref, start_pos)) || start_ref == 0)
        {
            return false;
        }
        if (dtStatusFailed(impl->nav_query->findNearestPoly(&end.x, extents, &impl->filter, &end_ref, end_pos)) || end_ref == 0)
        {
            return false;
        }

        dtPolyRef polys[max_path_polys] = {};
        int poly_count = 0;
        if (dtStatusFailed(impl->nav_query->findPath(start_ref, end_ref, start_pos, end_pos, &impl->filter, polys, &poly_count, max_path_polys)) || poly_count == 0)
        {
            return false;
        }

        float end_on_poly[3] = { end_pos[0], end_pos[1], end_pos[2] };
        if (polys[poly_count - 1] != end_ref)
        {
            impl->nav_query->closestPointOnPoly(polys[poly_count - 1], end_pos, end_on_poly, nullptr);
        }

        float straight[max_straight_points * 3] = {};
        int straight_count = 0;
        if (dtStatusFailed(impl->nav_query->findStraightPath(start_pos, end_on_poly, polys, poly_count, straight, nullptr, nullptr, &straight_count, max_straight_points)) || straight_count == 0)
        {
            return false;
        }

        out_path.reserve(straight_count);
        for (int i = 0; i < straight_count; ++i)
        {
            out_path.push_back({ straight[i * 3 + 0], straight[i * 3 + 1], straight[i * 3 + 2] });
        }
        return true;
    }
}
