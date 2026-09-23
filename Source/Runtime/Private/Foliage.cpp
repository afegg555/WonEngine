#include "Foliage.h"

#include "TerrainData.h"
#include "MathUtils.h"

#include <cmath>
#include <random>

namespace won::foliage
{
    namespace
    {
        constexpr uint32 max_instances_per_type = 200000;

        struct SurfaceSample
        {
            float3 position = {};
            float3 normal = { 0.0f, 1.0f, 0.0f };
            float layer_weights[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
            uint32 layer_count = 0;
        };

        bool SampleTerrainSurface(const terrain::TerrainData& data, float local_x, float local_z, SurfaceSample& out)
        {
			// terrain::SampleTerrainHeight + normal + material weights
            const float min_x = data.offset_x;
            const float max_x = data.offset_x + data.world_size_x;
            const float min_z = data.offset_z;
            const float max_z = data.offset_z + data.world_size_z;
            if (local_x < min_x || local_x > max_x || local_z < min_z || local_z > max_z)
            {
                return false;
            }

            float height = 0.0f;
            if (!terrain::SampleTerrainHeight(data, float2(local_x, local_z), height))
            {
                return false;
            }
            out.position = float3(local_x, height, local_z);

            const float epsilon = (std::max)(data.cell_x, data.cell_z);
            float height_x0 = height;
            float height_x1 = height;
            float height_z0 = height;
            float height_z1 = height;
            terrain::SampleTerrainHeight(data, float2(local_x - epsilon, local_z), height_x0);
            terrain::SampleTerrainHeight(data, float2(local_x + epsilon, local_z), height_x1);
            terrain::SampleTerrainHeight(data, float2(local_x, local_z - epsilon), height_z0);
            terrain::SampleTerrainHeight(data, float2(local_x, local_z + epsilon), height_z1);
            const float slope_x = (height_x1 - height_x0) / (2.0f * epsilon);
            const float slope_z = (height_z1 - height_z0) / (2.0f * epsilon);
            DirectX::XMStoreFloat3(&out.normal, DirectX::XMVector3Normalize(DirectX::XMVectorSet(-slope_x, 1.0f, -slope_z, 0.0f)));

            out.layer_count = 0;
            const Size layer_count = data.material_weights.size();
            const Size material_sample_count = static_cast<Size>(data.material_samples_x) * data.material_samples_z;
            if (layer_count > 0 && material_sample_count > 0 && data.material_samples_x > 1 && data.material_samples_z > 1)
            {
                const float grid_x = math::Clamp((local_x - min_x) / data.world_size_x, 0.0f, 1.0f) * static_cast<float>(data.material_samples_x - 1);
                const float grid_z = math::Clamp((local_z - min_z) / data.world_size_z, 0.0f, 1.0f) * static_cast<float>(data.material_samples_z - 1);
                const Size col = static_cast<Size>(grid_x + 0.5f);
                const Size row = static_cast<Size>(grid_z + 0.5f);
                const Size sample_index = row * data.material_samples_x + col;
                const Size usable_layers = (std::min<Size>)(layer_count, 4);
                for (Size layer = 0; layer < usable_layers; ++layer)
                {
                    if (sample_index < data.material_weights[layer].size())
                    {
                        out.layer_weights[layer] = static_cast<float>(data.material_weights[layer][sample_index]) / 255.0f;
                    }
                }
                out.layer_count = static_cast<uint32>(usable_layers);
            }
            return true;
        }
    }

    void ScatterFoliage(ecs::FoliageComponent& foliage, const terrain::TerrainData& terrain, const float4x4& terrain_world)
    {
        using namespace DirectX;

        const XMMATRIX world_matrix = XMLoadFloat4x4(&terrain_world);
        const XMMATRIX inverse_matrix = XMMatrixInverse(nullptr, world_matrix);
        const XMMATRIX normal_matrix = XMMatrixTranspose(inverse_matrix);

        const float3 center = foliage.region_center;
        const float3 extent = foliage.region_extent;
        const float min_x = center.x - extent.x;
        const float max_x = center.x + extent.x;
        const float min_z = center.z - extent.z;
        const float max_z = center.z + extent.z;
        const float area = (max_x - min_x) * (max_z - min_z);

        for (Size type_index = 0; type_index < foliage.types.size(); ++type_index)
        {
            ecs::FoliageType& type = foliage.types[type_index];
            type.instances.clear();
            if (type.density <= 0.0f || area <= 0.0f)
            {
                continue;
            }

            const uint32 candidate_count = static_cast<uint32>((std::min)(type.density * area, static_cast<float>(max_instances_per_type)));
            std::mt19937 rng(foliage.seed + 0x9E3779B9u * static_cast<uint32>(type_index + 1));
            std::uniform_real_distribution<float> unit(0.0f, 1.0f);

            for (uint32 candidate = 0; candidate < candidate_count; ++candidate)
            {
                const float world_x = math::Lerp(min_x, max_x, unit(rng));
                const float world_z = math::Lerp(min_z, max_z, unit(rng));

                const XMVECTOR local_query = XMVector3TransformCoord(XMVectorSet(world_x, 0.0f, world_z, 1.0f), inverse_matrix);

                SurfaceSample sample;
                if (!SampleTerrainSurface(terrain, XMVectorGetX(local_query), XMVectorGetZ(local_query), sample))
                {
                    continue;
                }

                float3 world_position;
                XMStoreFloat3(&world_position, XMVector3TransformCoord(XMLoadFloat3(&sample.position), world_matrix));
                float3 world_normal;
                XMStoreFloat3(&world_normal, XMVector3Normalize(XMVector3TransformNormal(XMLoadFloat3(&sample.normal), normal_matrix)));

                if (world_position.y < type.height_min || world_position.y > type.height_max)
                {
                    continue;
                }
                const float slope_degrees = math::RadiansToDegrees(std::acos(math::Clamp(world_normal.y, -1.0f, 1.0f)));
                if (slope_degrees < type.slope_min_degrees || slope_degrees > type.slope_max_degrees)
                {
                    continue;
                }
                if (type.terrain_layer >= 0)
                {
                    const float weight = (static_cast<uint32>(type.terrain_layer) < sample.layer_count)
                        ? sample.layer_weights[type.terrain_layer]
                        : 0.0f;
                    if (unit(rng) > weight)
                    {
                        continue;
                    }
                }

                const float yaw = (unit(rng) * 2.0f - 1.0f) * math::PI * math::Clamp(type.random_yaw, 0.0f, 1.0f);
                XMVECTOR rotation = XMQuaternionRotationRollPitchYaw(0.0f, yaw, 0.0f);
                if (type.align_to_normal > 0.0f)
                {
                    XMVECTOR axis = XMVector3Cross(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), XMLoadFloat3(&world_normal));
                    if (XMVectorGetX(XMVector3LengthSq(axis)) > 1e-10f)
                    {
                        const float angle = std::acos(math::Clamp(world_normal.y, -1.0f, 1.0f)) * math::Clamp(type.align_to_normal, 0.0f, 1.0f);
                        rotation = XMQuaternionMultiply(rotation, XMQuaternionRotationAxis(XMVector3Normalize(axis), angle));
                    }
                }

                ecs::FoliageInstance instance;
                instance.position = world_position;
                instance.scale = math::Lerp(type.scale_range.x, type.scale_range.y, unit(rng));
                XMStoreFloat4(&instance.rotation, XMQuaternionNormalize(rotation));
                type.instances.push_back(instance);
            }
        }

        foliage.SetDirty(false);
    }
}
