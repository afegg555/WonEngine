#include "TerrainGenerator.h"
#include "MathUtils.h"
#include "Noise.h"

#include <algorithm>
#include <cmath>

namespace won::ecs
{
    namespace
    {
        constexpr uint32 warp_x_seed_offset = 17u;
        constexpr uint32 warp_z_seed_offset = 31u;
        constexpr float warp_frequency_scale = 0.5f; // warp field is lower frequency than the base terrain, for broad smooth bending instead of high-frequency jitter

        float TerrainHeight(float x, float z, const TerrainComponent& terrain)
        {
            // fractal noise means layering multiple octaves of noise together to create more complex patterns
            noise::FractalNoiseDesc desc = {};
            desc.frequency = (std::max)(terrain.frequency, 0.0001f);
            desc.octaves = terrain.octaves > 0 ? terrain.octaves : 1;
            desc.lacunarity = (std::max)(terrain.lacunarity, 0.0001f);
            desc.persistence = (std::max)(terrain.persistence, 0.0f);
            desc.seed = terrain.seed;

            const float noise_wavelength = 1.0f / desc.frequency;
            const float warp_amount = (std::max)(terrain.warp_strength, 0.0f);
            noise::FractalNoiseDesc warp_x_desc = desc;
            noise::FractalNoiseDesc warp_z_desc = desc;
            warp_x_desc.seed += warp_x_seed_offset;
            warp_z_desc.seed += warp_z_seed_offset;
            warp_x_desc.frequency = desc.frequency * warp_frequency_scale;
            warp_z_desc.frequency = desc.frequency * warp_frequency_scale;
            const float warp_x = noise::FractalBrownianMotion2D(x, z, warp_x_desc) * noise_wavelength * warp_amount;
            const float warp_z = noise::FractalBrownianMotion2D(x, z, warp_z_desc) * noise_wavelength * warp_amount;
            const float sample_x = x + warp_x;
            const float sample_z = z + warp_z;

            const float ridge_strength = (std::max)(terrain.ridge_strength, 0.0f);
            const float base = noise::FractalBrownianMotion2D(sample_x, sample_z, desc);
            const float ridge = (noise::Ridged2D(sample_x, sample_z, desc) * 2.0f - 1.0f) * ridge_strength;
            float height = (base + ridge) / (1.0f + ridge_strength);

            const float island_falloff = math::saturate(terrain.island_falloff);
            if (island_falloff > 0.0f)
            {
                const float nx = terrain.world_size_x > 0.0f ? (x / (terrain.world_size_x * 0.5f)) : 0.0f;
                const float nz = terrain.world_size_z > 0.0f ? (z / (terrain.world_size_z * 0.5f)) : 0.0f;
                const float distance = std::sqrt(nx * nx + nz * nz);
                const float island_mask = 1.0f - math::SmoothStep(0.35f, 1.0f, distance);
                const float island_height = height * island_mask - (1.0f - island_mask) * 0.35f;
                height = math::Lerp(height, island_height, island_falloff);
            }

            return math::clamp(height, -1.0f, 1.0f);
        }
    }

    std::shared_ptr<resource::Mesh> GenerateTerrainMesh(const TerrainComponent& terrain)
    {
        const uint32 res_x = terrain.resolution_x < 1 ? 1 : terrain.resolution_x;
        const uint32 res_z = terrain.resolution_z < 1 ? 1 : terrain.resolution_z;
        const uint32 vert_x = res_x + 1;
        const uint32 vert_z = res_z + 1;

        const float size_x = terrain.world_size_x;
        const float size_z = terrain.world_size_z;
        const float half_x = size_x * 0.5f;
        const float half_z = size_z * 0.5f;
        const float cell_x = size_x / static_cast<float>(res_x);
        const float cell_z = size_z / static_cast<float>(res_z);

        auto mesh = std::make_shared<resource::Mesh>();
        const uint32 vertex_count = vert_x * vert_z;
        mesh->positions.resize(vertex_count);
        mesh->normals.resize(vertex_count);
        mesh->texcoords.resize(vertex_count);

        // Precompute the height field first so normals can use neighbor differences.
        Vector<float> heights(vertex_count);
        for (uint32 j = 0; j < vert_z; ++j)
        {
            for (uint32 i = 0; i < vert_x; ++i)
            {
                const float x = -half_x + static_cast<float>(i) * cell_x;
                const float z = -half_z + static_cast<float>(j) * cell_z;
                heights[j * vert_x + i] = TerrainHeight(x, z, terrain) * terrain.height_scale;
            }
        }

        math::AABB bounds = {};
        bounds.Invalidate();
        for (uint32 j = 0; j < vert_z; ++j)
        {
            for (uint32 i = 0; i < vert_x; ++i)
            {
                const uint32 index = j * vert_x + i;
                const float x = -half_x + static_cast<float>(i) * cell_x;
                const float z = -half_z + static_cast<float>(j) * cell_z;
                const float y = heights[index];
                mesh->positions[index] = { x, y, z };
                mesh->texcoords[index] = { static_cast<float>(i) / static_cast<float>(res_x), static_cast<float>(j) / static_cast<float>(res_z) };

                // Heightfield normal from central differences of the height field.
                const uint32 il = i > 0 ? i - 1 : i;
                const uint32 ir = i + 1 < vert_x ? i + 1 : i;
                const uint32 jd = j > 0 ? j - 1 : j;
                const uint32 ju = j + 1 < vert_z ? j + 1 : j;
                const float hl = heights[j * vert_x + il];
                const float hr = heights[j * vert_x + ir];
                const float hd = heights[jd * vert_x + i];
                const float hu = heights[ju * vert_x + i];
                const float dx = static_cast<float>(ir - il) * cell_x;
                const float dz = static_cast<float>(ju - jd) * cell_z;
                float3 normal = { -(hr - hl) / (dx > 0.0f ? dx : 1.0f), 1.0f, -(hu - hd) / (dz > 0.0f ? dz : 1.0f) };
                const float length = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
                if (length > 0.0f)
                {
                    normal.x /= length;
                    normal.y /= length;
                    normal.z /= length;
                }
                mesh->normals[index] = normal;

                bounds.min.x = (std::min)(bounds.min.x, x);
                bounds.min.y = (std::min)(bounds.min.y, y);
                bounds.min.z = (std::min)(bounds.min.z, z);
                bounds.max.x = (std::max)(bounds.max.x, x);
                bounds.max.y = (std::max)(bounds.max.y, y);
                bounds.max.z = (std::max)(bounds.max.z, z);
            }
        }

        // Two triangles per grid cell. Winding chosen for top-facing (+Y) front faces under
        // the engine's CW front-face convention; verify visually and flip if back-face culled.
        mesh->indices.reserve(static_cast<Size>(res_x) * res_z * 6);
        for (uint32 j = 0; j < res_z; ++j)
        {
            for (uint32 i = 0; i < res_x; ++i)
            {
                const uint32 i00 = j * vert_x + i;
                const uint32 i10 = j * vert_x + (i + 1);
                const uint32 i01 = (j + 1) * vert_x + i;
                const uint32 i11 = (j + 1) * vert_x + (i + 1);
                mesh->indices.push_back(i00);
                mesh->indices.push_back(i01);
                mesh->indices.push_back(i11);
                mesh->indices.push_back(i00);
                mesh->indices.push_back(i11);
                mesh->indices.push_back(i10);
            }
        }

        resource::Submesh submesh = {};
        submesh.first_index = 0;
        submesh.index_count = static_cast<uint32>(mesh->indices.size());
        submesh.first_vertex = 0;
        submesh.material_slot = 0;
        submesh.primitive_topology = resource::PrimitiveTopology::TriangleList;
        submesh.local_bounds = bounds;
        mesh->submeshes.push_back(submesh);

        return mesh;
    }
}
