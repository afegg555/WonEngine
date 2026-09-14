#include "TerrainData.h"
#include "MathUtils.h"
#include "Noise.h"
#include "BinaryArchive.h"
#include "ResourceExtension.h"
#include "FileSystem.h"
#include "StringUtils.h"
#include "Backlog.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace won::serialize
{
    void Serialize(BinaryArchive& archive, won::ecs::TerrainSpline& spline)
    {
        Serialize(archive, spline.points);
        Serialize(archive, spline.width);
        Serialize(archive, spline.edge_falloff);
        Serialize(archive, spline.target_height);
    }
}

namespace won::ecs
{
    namespace
    {
        constexpr uint32 warp_x_seed_offset = 17u;
        constexpr uint32 warp_z_seed_offset = 31u;
        constexpr float warp_frequency_scale = 0.5f; // warp field is lower frequency than the base terrain, for broad smooth bending instead of high-frequency jitter

        float TerrainHeight(float x, float z, const TerrainNoiseParams& np, float world_size_x, float world_size_z)
        {
            // fractal noise means layering multiple octaves of noise together to create more complex patterns
            noise::FractalNoiseDesc desc = {};
            desc.frequency = (std::max)(np.frequency, 0.0001f);
            desc.octaves = np.octaves > 0 ? np.octaves : 1;
            desc.lacunarity = (std::max)(np.lacunarity, 0.0001f);
            desc.persistence = (std::max)(np.persistence, 0.0f);
            desc.seed = np.seed;

            const float noise_wavelength = 1.0f / desc.frequency;
            const float warp_amount = (std::max)(np.warp_strength, 0.0f);
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

            const float ridge_strength = (std::max)(np.ridge_strength, 0.0f);
            const float base = noise::FractalBrownianMotion2D(sample_x, sample_z, desc);
            const float ridge = (noise::Ridged2D(sample_x, sample_z, desc) * 2.0f - 1.0f) * ridge_strength;
            float height = (base + ridge) / (1.0f + ridge_strength);

            const float island_falloff = math::Saturate(np.island_falloff);
            if (island_falloff > 0.0f)
            {
                const float nx = world_size_x > 0.0f ? (x / (world_size_x * 0.5f)) : 0.0f;
                const float nz = world_size_z > 0.0f ? (z / (world_size_z * 0.5f)) : 0.0f;
                const float distance = std::sqrt(nx * nx + nz * nz);
                const float island_mask = 1.0f - math::SmoothStep(0.35f, 1.0f, distance);
                const float island_height = height * island_mask - (1.0f - island_mask) * 0.35f;
                height = math::Lerp(height, island_height, island_falloff);
            }

            return math::Clamp(height, -1.0f, 1.0f);
        }
    }

    void BakeTerrainNoise(TerrainData& data)
    {
        if (data.samples_x < 2 || data.samples_z < 2)
        {
            return;
        }

        data.cell_x = data.world_size_x / static_cast<float>(data.samples_x - 1);
        data.cell_z = data.world_size_z / static_cast<float>(data.samples_z - 1);
        data.offset_x = -data.world_size_x * 0.5f;
        data.offset_z = -data.world_size_z * 0.5f;

        const Size n = static_cast<Size>(data.samples_x) * data.samples_z;
        data.base_heights.resize(n);
        for (uint32 j = 0; j < data.samples_z; ++j)
        {
            for (uint32 i = 0; i < data.samples_x; ++i)
            {
                const float x = data.offset_x + static_cast<float>(i) * data.cell_x;
                const float z = data.offset_z + static_cast<float>(j) * data.cell_z;
                data.base_heights[static_cast<Size>(j) * data.samples_x + i] = TerrainHeight(x, z, data.noise_params, data.world_size_x, data.world_size_z) * data.height_scale;
            }
        }

        CompositeTerrainHeights(data);
    }

    bool SampleTerrainHeight(const TerrainData& data, const float2& local_position, float& out_height)
    {
        const Size sample_count = static_cast<Size>(data.samples_x) * data.samples_z;
        if (!data.IsValid() || data.final_heights.size() != sample_count || data.cell_x <= 0.0f || data.cell_z <= 0.0f)
        {
            return false;
        }

        const float grid_x = math::Clamp((local_position.x - data.offset_x) / data.cell_x, 0.0f, static_cast<float>(data.samples_x - 1));
        const float grid_z = math::Clamp((local_position.y - data.offset_z) / data.cell_z, 0.0f, static_cast<float>(data.samples_z - 1));
        const uint32 x0 = static_cast<uint32>(std::floor(grid_x));
        const uint32 z0 = static_cast<uint32>(std::floor(grid_z));
        const uint32 x1 = (std::min)(x0 + 1, data.samples_x - 1);
        const uint32 z1 = (std::min)(z0 + 1, data.samples_z - 1);
        const float tx = grid_x - static_cast<float>(x0);
        const float tz = grid_z - static_cast<float>(z0);
        const float height0 = math::Lerp(data.final_heights[static_cast<Size>(z0) * data.samples_x + x0], data.final_heights[static_cast<Size>(z0) * data.samples_x + x1], tx);
        const float height1 = math::Lerp(data.final_heights[static_cast<Size>(z1) * data.samples_x + x0], data.final_heights[static_cast<Size>(z1) * data.samples_x + x1], tx);
        out_height = math::Lerp(height0, height1, tz);
        return true;
    }

    bool RayCastTerrain(const TerrainData& data, const math::Ray& local_ray, float3& out_local_hit)
    {
        const Size sample_count = static_cast<Size>(data.samples_x) * data.samples_z;
        if (!data.IsValid() || data.final_heights.size() != sample_count || data.cell_x <= 0.0f || data.cell_z <= 0.0f)
        {
            return false;
        }

        const float3 local_origin = local_ray.origin;
        const float3 local_direction = local_ray.direction;
        const float min_x = data.offset_x;
        const float max_x = data.offset_x + data.world_size_x;
        const float min_z = data.offset_z;
        const float max_z = data.offset_z + data.world_size_z;
        float enter_distance = 0.0f;
        float exit_distance = (std::numeric_limits<float>::max)();
        if (std::abs(local_direction.x) < 0.000001f)
        {
            if (local_origin.x < min_x || local_origin.x > max_x)
            {
                return false;
            }
        }
        else
        {
            float distance0 = (min_x - local_origin.x) / local_direction.x;
            float distance1 = (max_x - local_origin.x) / local_direction.x;
            if (distance0 > distance1)
            {
                std::swap(distance0, distance1);
            }
            enter_distance = (std::max)(enter_distance, distance0);
            exit_distance = (std::min)(exit_distance, distance1);
        }
        if (std::abs(local_direction.z) < 0.000001f)
        {
            if (local_origin.z < min_z || local_origin.z > max_z)
            {
                return false;
            }
        }
        else
        {
            float distance0 = (min_z - local_origin.z) / local_direction.z;
            float distance1 = (max_z - local_origin.z) / local_direction.z;
            if (distance0 > distance1)
            {
                std::swap(distance0, distance1);
            }
            enter_distance = (std::max)(enter_distance, distance0);
            exit_distance = (std::min)(exit_distance, distance1);
        }
        if (exit_distance < enter_distance)
        {
            return false;
        }

        const float start_distance = enter_distance + 0.0001f;
        const float start_x = local_origin.x + local_direction.x * start_distance;
        const float start_z = local_origin.z + local_direction.z * start_distance;
        int cell_x = math::Clamp(static_cast<int>(std::floor((start_x - min_x) / data.cell_x)), 0, static_cast<int>(data.samples_x) - 2);
        int cell_z = math::Clamp(static_cast<int>(std::floor((start_z - min_z) / data.cell_z)), 0, static_cast<int>(data.samples_z) - 2);
        const int step_x = local_direction.x > 0.0f ? 1 : local_direction.x < 0.0f ? -1 : 0;
        const int step_z = local_direction.z > 0.0f ? 1 : local_direction.z < 0.0f ? -1 : 0;
        const float infinite_distance = (std::numeric_limits<float>::max)();
        const float next_x = min_x + static_cast<float>(step_x > 0 ? cell_x + 1 : cell_x) * data.cell_x;
        const float next_z = min_z + static_cast<float>(step_z > 0 ? cell_z + 1 : cell_z) * data.cell_z;
        float boundary_x = step_x == 0 ? infinite_distance : (next_x - local_origin.x) / local_direction.x;
        float boundary_z = step_z == 0 ? infinite_distance : (next_z - local_origin.z) / local_direction.z;
        const float cell_distance_x = step_x == 0 ? infinite_distance : data.cell_x / std::abs(local_direction.x);
        const float cell_distance_z = step_z == 0 ? infinite_distance : data.cell_z / std::abs(local_direction.z);
        const XMVECTOR ray_origin = XMLoadFloat3(&local_origin);
        const XMVECTOR ray_direction = XMLoadFloat3(&local_direction);
        float current_distance = enter_distance;

        while (cell_x >= 0 && cell_x + 1 < static_cast<int>(data.samples_x) && cell_z >= 0 && cell_z + 1 < static_cast<int>(data.samples_z) && current_distance <= exit_distance)
        {
            const float cell_end_distance = (std::min)((std::min)(boundary_x, boundary_z), exit_distance);
            const uint32 x0 = static_cast<uint32>(cell_x);
            const uint32 z0 = static_cast<uint32>(cell_z);
            const uint32 x1 = x0 + 1;
            const uint32 z1 = z0 + 1;
            const XMVECTOR point00 = XMVectorSet(min_x + static_cast<float>(x0) * data.cell_x, data.final_heights[static_cast<Size>(z0) * data.samples_x + x0], min_z + static_cast<float>(z0) * data.cell_z, 1.0f);
            const XMVECTOR point10 = XMVectorSet(min_x + static_cast<float>(x1) * data.cell_x, data.final_heights[static_cast<Size>(z0) * data.samples_x + x1], min_z + static_cast<float>(z0) * data.cell_z, 1.0f);
            const XMVECTOR point01 = XMVectorSet(min_x + static_cast<float>(x0) * data.cell_x, data.final_heights[static_cast<Size>(z1) * data.samples_x + x0], min_z + static_cast<float>(z1) * data.cell_z, 1.0f);
            const XMVECTOR point11 = XMVectorSet(min_x + static_cast<float>(x1) * data.cell_x, data.final_heights[static_cast<Size>(z1) * data.samples_x + x1], min_z + static_cast<float>(z1) * data.cell_z, 1.0f);
            float hit_distance0 = 0.0f;
            float hit_distance1 = 0.0f;
            float2 barycentric = {};
            const bool hit0 = math::RayTriangleIntersects(ray_origin, ray_direction, point00, point01, point11, hit_distance0, barycentric, current_distance, cell_end_distance + 0.0001f);
            const bool hit1 = math::RayTriangleIntersects(ray_origin, ray_direction, point00, point11, point10, hit_distance1, barycentric, current_distance, cell_end_distance + 0.0001f);
            if (hit0 || hit1)
            {
                const float hit_distance = hit0 && hit1 ? (std::min)(hit_distance0, hit_distance1) : hit0 ? hit_distance0 : hit_distance1;
                XMStoreFloat3(&out_local_hit, ray_origin + ray_direction * hit_distance);
                return true;
            }

            if (cell_end_distance >= exit_distance)
            {
                break;
            }
            if (boundary_x < boundary_z)
            {
                cell_x += step_x;
                current_distance = boundary_x;
                boundary_x += cell_distance_x;
            }
            else if (boundary_z < boundary_x)
            {
                cell_z += step_z;
                current_distance = boundary_z;
                boundary_z += cell_distance_z;
            }
            else
            {
                cell_x += step_x;
                cell_z += step_z;
                current_distance = boundary_x;
                boundary_x += cell_distance_x;
                boundary_z += cell_distance_z;
            }
        }

        return false;
    }

    std::shared_ptr<resource::Mesh> GenerateTerrainMesh(const TerrainData& data)
    {
        const Size sample_count = static_cast<Size>(data.samples_x) * data.samples_z;
        if (!data.IsValid() || data.final_heights.size() != sample_count)
        {
            return nullptr;
        }

        const Vector<float>& heights = data.final_heights;
        const uint32 vert_x = data.samples_x;
        const uint32 vert_z = data.samples_z;
        const uint32 res_x = vert_x - 1;
        const uint32 res_z = vert_z - 1;
        const float half_x = -data.offset_x;
        const float half_z = -data.offset_z;
        const float cell_x = data.cell_x;
        const float cell_z = data.cell_z;

        auto mesh = std::make_shared<resource::Mesh>();
        const uint32 vertex_count = vert_x * vert_z;
        mesh->positions.resize(vertex_count);
        mesh->normals.resize(vertex_count);
        mesh->texcoords.resize(vertex_count);

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

    namespace
    {
        float DistancePointToSegment(float px, float pz, const float2& a, const float2& b)
        {
            const float abx = b.x - a.x;
            const float abz = b.y - a.y;
            const float len_sq = abx * abx + abz * abz;
            float t = 0.0f;
            if (len_sq > 0.0f)
            {
                t = ((px - a.x) * abx + (pz - a.y) * abz) / len_sq;
                t = math::Clamp(t, 0.0f, 1.0f);
            }
            const float cx = a.x + abx * t;
            const float cz = a.y + abz * t;
            const float dx = px - cx;
            const float dz = pz - cz;
            return std::sqrt(dx * dx + dz * dz);
        }

        float SampleGridBilinear(const Vector<float>& src, uint32 w, uint32 h, float fx, float fz)
        {
            fx = math::Clamp(fx, 0.0f, static_cast<float>(w - 1));
            fz = math::Clamp(fz, 0.0f, static_cast<float>(h - 1));
            const uint32 x0 = static_cast<uint32>(fx);
            const uint32 z0 = static_cast<uint32>(fz);
            const uint32 x1 = x0 + 1 < w ? x0 + 1 : x0;
            const uint32 z1 = z0 + 1 < h ? z0 + 1 : z0;
            const float tx = fx - static_cast<float>(x0);
            const float tz = fz - static_cast<float>(z0);
            const float h00 = src[static_cast<Size>(z0) * w + x0];
            const float h10 = src[static_cast<Size>(z0) * w + x1];
            const float h01 = src[static_cast<Size>(z1) * w + x0];
            const float h11 = src[static_cast<Size>(z1) * w + x1];
            return math::Lerp(math::Lerp(h00, h10, tx), math::Lerp(h01, h11, tx), tz);
        }

        constexpr uint32 terrain_binary_magic = 0x4e525754u;
        constexpr uint32 terrain_binary_version = 1u;
    }

    void CompositeTerrainHeights(TerrainData& data)
    {
        const uint32 sx = data.samples_x;
        const uint32 sz = data.samples_z;
        const Size n = static_cast<Size>(sx) * sz;
        if (sx < 2 || sz < 2 || data.base_heights.size() != n)
        {
            data.final_heights.clear();
            return;
        }

        data.cell_x = data.world_size_x / static_cast<float>(sx - 1);
        data.cell_z = data.world_size_z / static_cast<float>(sz - 1);
        data.offset_x = -data.world_size_x * 0.5f;
        data.offset_z = -data.world_size_z * 0.5f;

        data.final_heights.resize(n);
        const bool has_delta = data.height_delta.size() == n;
        const bool has_flatten = data.flatten_mask.size() == n && data.flatten_height.size() == n;
        for (Size i = 0; i < n; ++i)
        {
            float height = data.base_heights[i];
            if (has_delta)
            {
                height += data.height_delta[i];
            }
            if (has_flatten && data.flatten_mask[i] != 0)
            {
                height = data.flatten_height[i];
            }
            data.final_heights[i] = height;
        }

        for (const TerrainSpline& spline : data.splines)
        {
            if (spline.points.size() < 2)
            {
                continue;
            }
            const float half_width = spline.width * 0.5f;
            const float falloff = spline.edge_falloff > 0.0f ? spline.edge_falloff : 0.0f;
            const float outer = half_width + falloff;
            for (uint32 j = 0; j < sz; ++j)
            {
                for (uint32 i = 0; i < sx; ++i)
                {
                    const float wx = data.offset_x + static_cast<float>(i) * data.cell_x;
                    const float wz = data.offset_z + static_cast<float>(j) * data.cell_z;
                    float nearest = outer + 1.0f;
                    for (Size s = 0; s + 1 < spline.points.size(); ++s)
                    {
                        const float d = DistancePointToSegment(wx, wz, spline.points[s], spline.points[s + 1]);
                        if (d < nearest)
                        {
                            nearest = d;
                        }
                    }
                    if (nearest > outer)
                    {
                        continue;
                    }
                    float weight = 1.0f;
                    if (nearest > half_width && falloff > 0.0f)
                    {
                        weight = 1.0f - (nearest - half_width) / falloff;
                    }
                    const Size idx = static_cast<Size>(j) * sx + i;
                    data.final_heights[idx] = math::Lerp(data.final_heights[idx], spline.target_height, weight);
                }
            }
        }
    }

    void ResizeTerrainEditLayer(TerrainData& data, uint32 new_samples_x, uint32 new_samples_z)
    {
        if (new_samples_x < 2 || new_samples_z < 2)
        {
            return;
        }

        const uint32 old_x = data.samples_x;
        const uint32 old_z = data.samples_z;
        if (old_x < 2 || old_z < 2)
        {
            data.samples_x = new_samples_x;
            data.samples_z = new_samples_z;
            return;
        }

        const Size old_n = static_cast<Size>(old_x) * old_z;
        const Size new_n = static_cast<Size>(new_samples_x) * new_samples_z;

        auto resample_float = [&](const Vector<float>& src) -> Vector<float>
        {
            Vector<float> dst;
            if (src.size() != old_n)
            {
                return dst;
            }
            dst.resize(new_n);
            for (uint32 j = 0; j < new_samples_z; ++j)
            {
                const float fz = static_cast<float>(j) * static_cast<float>(old_z - 1) / static_cast<float>(new_samples_z - 1);
                for (uint32 i = 0; i < new_samples_x; ++i)
                {
                    const float fx = static_cast<float>(i) * static_cast<float>(old_x - 1) / static_cast<float>(new_samples_x - 1);
                    dst[static_cast<Size>(j) * new_samples_x + i] = SampleGridBilinear(src, old_x, old_z, fx, fz);
                }
            }
            return dst;
        };

        data.height_delta = resample_float(data.height_delta);
        data.flatten_height = resample_float(data.flatten_height);

        if (data.flatten_mask.size() == old_n)
        {
            Vector<float> mask_f(old_n);
            for (Size i = 0; i < old_n; ++i)
            {
                mask_f[i] = data.flatten_mask[i] != 0 ? 1.0f : 0.0f;
            }
            Vector<uint8> new_mask(new_n);
            for (uint32 j = 0; j < new_samples_z; ++j)
            {
                const float fz = static_cast<float>(j) * static_cast<float>(old_z - 1) / static_cast<float>(new_samples_z - 1);
                for (uint32 i = 0; i < new_samples_x; ++i)
                {
                    const float fx = static_cast<float>(i) * static_cast<float>(old_x - 1) / static_cast<float>(new_samples_x - 1);
                    new_mask[static_cast<Size>(j) * new_samples_x + i] = SampleGridBilinear(mask_f, old_x, old_z, fx, fz) >= 0.5f ? 1u : 0u;
                }
            }
            data.flatten_mask = std::move(new_mask);
        }

        data.samples_x = new_samples_x;
        data.samples_z = new_samples_z;
    }

    bool SaveTerrainBinary(const String& path, const TerrainData& data)
    {
        if (path.empty())
        {
            return false;
        }

        TerrainData copy = data;
        copy.final_heights.clear();

        serialize::BinaryArchive archive(path, serialize::ArchiveMode::Write);
        uint32 magic = terrain_binary_magic;
        uint32 version = terrain_binary_version;
        serialize::Serialize(archive, magic);
        serialize::Serialize(archive, version);
        serialize::Serialize(archive, copy.samples_x);
        serialize::Serialize(archive, copy.samples_z);
        serialize::Serialize(archive, copy.world_size_x);
        serialize::Serialize(archive, copy.world_size_z);
        serialize::Serialize(archive, copy.height_scale);
        serialize::Serialize(archive, copy.base_heights);
        serialize::Serialize(archive, copy.height_delta);
        serialize::Serialize(archive, copy.flatten_mask);
        serialize::Serialize(archive, copy.flatten_height);
        serialize::Serialize(archive, copy.splines);
        serialize::Serialize(archive, copy.noise_params);
        return true;
    }

    std::shared_ptr<TerrainData> LoadTerrainBinary(const String& path)
    {
        if (path.empty() || !io::Exists(path))
        {
            return nullptr;
        }
        if (utils::ToLower(io::GetExtension(path)) != resource::terrain_binary_extension)
        {
            backlog::Post("[LoadResources] terrain load rejected, expected ." + String(resource::terrain_binary_extension) + ": " + path, backlog::LogLevel::Warning);
            return nullptr;
        }

        serialize::BinaryArchive archive(path, serialize::ArchiveMode::Read);
        uint32 magic = 0;
        uint32 version = 0;
        serialize::Serialize(archive, magic);
        serialize::Serialize(archive, version);
        if (magic != terrain_binary_magic)
        {
            backlog::Post("[LoadResources] terrain load failed, bad magic: " + path, backlog::LogLevel::Warning);
            return nullptr;
        }

        auto data = std::make_shared<TerrainData>();
        serialize::Serialize(archive, data->samples_x);
        serialize::Serialize(archive, data->samples_z);
        serialize::Serialize(archive, data->world_size_x);
        serialize::Serialize(archive, data->world_size_z);
        serialize::Serialize(archive, data->height_scale);
        serialize::Serialize(archive, data->base_heights);
        serialize::Serialize(archive, data->height_delta);
        serialize::Serialize(archive, data->flatten_mask);
        serialize::Serialize(archive, data->flatten_height);
        serialize::Serialize(archive, data->splines);
        serialize::Serialize(archive, data->noise_params);
        CompositeTerrainHeights(*data);
        return data;
    }
}
