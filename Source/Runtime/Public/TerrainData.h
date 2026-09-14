#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "Mesh.h"
#include "Primitives.h"

#include <memory>

namespace won::ecs
{
    struct TerrainNoiseParams
    {
        uint32 seed = 0;
        float frequency = 0.03f;
        uint32 octaves = 4;
        float lacunarity = 2.0f;
        float persistence = 0.5f;
        float ridge_strength = 0.0f;
        float warp_strength = 0.0f;
        float island_falloff = 0.0f;
    };

    struct TerrainSpline
    {
        Vector<float2> points;
        float width = 4.0f;
        float edge_falloff = 2.0f;
        float target_height = 0.0f;
    };

    struct TerrainData
    {
        uint32 samples_x = 0;
        uint32 samples_z = 0;
        float world_size_x = 100.0f;
        float world_size_z = 100.0f;
        float height_scale = 10.0f;

		Vector<float> base_heights; // generated terrain heights(noise)
		Vector<float> height_delta; // user edited height delta, added to base_heights
		Vector<uint8> flatten_mask; // user edited flatten mask, 0 = no flatten, 1 = flatten to flatten_height
        Vector<float> flatten_height;
		Vector<TerrainSpline> splines; // user edited splines

        TerrainNoiseParams noise_params;

        Vector<float> final_heights; // final terrain heights after applying edits

        float cell_x = 0.0f;
        float cell_z = 0.0f;
        float offset_x = 0.0f;
        float offset_z = 0.0f;

        bool IsValid() const
        {
            return samples_x > 1 && samples_z > 1 &&
                base_heights.size() == static_cast<Size>(samples_x) * samples_z;
        }
    };

    WONENGINE_API void BakeTerrainNoise(TerrainData& data);
    WONENGINE_API void CompositeTerrainHeights(TerrainData& data);
    WONENGINE_API void ResizeTerrainEditLayer(TerrainData& data, uint32 new_samples_x, uint32 new_samples_z);
    WONENGINE_API bool RayCastTerrain(const TerrainData& data, const math::Ray& local_ray, float3& out_local_hit);

    WONENGINE_API std::shared_ptr<resource::Mesh> GenerateTerrainMesh(const TerrainData& data);

    WONENGINE_API bool SaveTerrainBinary(const String& path, const TerrainData& data);
    WONENGINE_API std::shared_ptr<TerrainData> LoadTerrainBinary(const String& path);
}
