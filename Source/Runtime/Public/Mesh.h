#pragma once

#include "Primitives.h"
#include "Types.h"

namespace won::resource
{
    struct MeshSubmesh
    {
        uint32 first_index = 0;
        uint32 index_count = 0;
        uint32 first_vertex = 0;
        uint32 material_slot = 0;
        math::Aabb local_bounds = {};
    };

    struct Mesh
    {
        Vector<float3> positions;
        Vector<float3> normals;
        Vector<float2> texcoords;
        Vector<uint32> indices;
        Vector<MeshSubmesh> submeshes;

        bool IsValid() const
        {
            return !positions.empty() && !indices.empty();
        }
    };
}
