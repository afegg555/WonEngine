#include "WaterSurface.h"
#include "Scene.h"
#include "GPUScene.h"
#include "ShaderInterop_Water.h"

#include <cmath>

namespace won::water
{
    SurfaceSample SampleSurface(ecs::Scene& scene, float2 world_xz)
    {
        SurfaceSample sample = {};

        const rendering::GPUScene& gpu_scene = scene.GetGPUScene();
        const ShaderWaterBody* selected = nullptr;
        for (const ShaderWaterBody& body : gpu_scene.water.shader_bodies)
        {
            if ((body.flags & SHADER_WATER_FLAG_ACTIVE) == 0)
            {
                continue;
            }

            const float offset_x = world_xz.x - body.plane_origin.x;
            const float offset_z = world_xz.y - body.plane_origin.z;
            const float u = (offset_x * body.axis_x.x + offset_z * body.axis_x.z) / (std::max)(body.half_extent_x, 0.0001f);
            const float v = (offset_x * body.axis_z.x + offset_z * body.axis_z.z) / (std::max)(body.half_extent_z, 0.0001f);
            if (std::abs(u) > 1.0f || std::abs(v) > 1.0f)
            {
                continue;
            }

            if (!selected || body.plane_origin.y > selected->plane_origin.y)
            {
                selected = &body;
            }
        }

        if (!selected)
        {
            return sample;
        }

        const float wave_time = static_cast<float>(scene.GetSimulation().elapsed_seconds);
        const WaterSurfaceSample wave = EvaluateWaterSurface(world_xz, selected->wave_direction, selected->wave_direction_spread,
            selected->wave_count, selected->wave_length, selected->wave_amplitude, selected->wave_steepness, wave_time, 1.0f);

        const float3 normal = { -wave.gradient.x, 1.0f, -wave.gradient.y };
        const float normal_length = (std::max)(std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z), 0.0001f);

        sample.height = selected->plane_origin.y + wave.height;
        sample.normal = { normal.x / normal_length, normal.y / normal_length, normal.z / normal_length };
        sample.velocity = { wave.velocity_horizontal.x, wave.velocity_vertical, wave.velocity_horizontal.y };
        sample.valid = true;
        return sample;
    }
}
