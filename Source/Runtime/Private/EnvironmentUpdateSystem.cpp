#include "EnvironmentUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "SkyComponent.h"
#include "TransformComponent.h"
#include "JobSystem.h"
#include "Backlog.h"

namespace won::ecs
{
    void EnvironmentUpdateSystem::Update(Scene& scene, float delta_time)
    {
        auto& render_data = scene.GetRenderData();
        render_data.shader_sky.Init();
        render_data.shader_environment_lighting.Init();
        render_data.shader_ddgi_volume.Init();
        render_data.ddgi_volume_entity = INVALID_ENTITY;

        auto sky_array = scene.GetComponentArray<SkyComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();

        if (sky_array && sky_array->GetSize() > 0)
        {
            SkyComponent& sky = sky_array->data[0]; // force to use first element
            if (sky.IsActive())
            {
                render_data.shader_sky.flags = sky.flags;
                render_data.shader_sky.SetSunDirection(sky.sun_direction);
                render_data.shader_sky.SetSunColorIntensity(sky.sun_color, sky.sun_intensity);
                render_data.shader_sky.SetSunParams(sky.sun_angular_radius, sky.sun_glow_intensity, sky.sun_glow_falloff);
                render_data.shader_sky.SetSkyHorizonColorIntensity(sky.sky_horizon_color, sky.sky_intensity);
                render_data.shader_sky.SetSkyZenithColorFalloff(sky.sky_zenith_color, sky.sky_horizon_falloff);
                render_data.shader_sky.SetGroundHorizonColorIntensity(sky.ground_horizon_color, sky.ground_intensity);
                render_data.shader_sky.SetGroundColorFalloff(sky.ground_color, sky.ground_falloff);
            }
        }

        auto env_lighting_array = scene.GetComponentArray<EnvironmentLightingComponent>().get();

        if (env_lighting_array && env_lighting_array->GetSize() > 0)
        {
            EnvironmentLightingComponent& env_lighting = env_lighting_array->data[0]; // force to use first element
            if (env_lighting.IsActive())
            {
                render_data.shader_environment_lighting.flags = SHADER_ENVIRONMENT_LIGHTING_FLAG_ACTIVE;
                render_data.shader_environment_lighting.gi_mode = static_cast<uint32>(env_lighting.gi_mode);
                render_data.shader_environment_lighting.SetAmbientColorIntensity(env_lighting.ambient_color, env_lighting.ambient_intensity);
                render_data.shader_environment_lighting.SetIndirectScale(env_lighting.indirect_diffuse_scale, env_lighting.indirect_specular_scale);
            }
        }

        auto ddgi_volume_array = scene.GetComponentArray<DDGIVolumeComponent>().get();
        if (ddgi_volume_array && ddgi_volume_array->GetSize() > 0)
        {
            DDGIVolumeComponent* selected_ddgi_volume = nullptr;
            Entity selected_ddgi_volume_entity = INVALID_ENTITY;
            float3 selected_volume_center = { 0.0f, 0.0f, 0.0f };

            for (Size i = 0; i < ddgi_volume_array->GetSize(); ++i)
            {
                DDGIVolumeComponent& ddgi_volume = ddgi_volume_array->data[i];
                if (!ddgi_volume.IsActive())
                {
                    continue;
                }

                if (selected_ddgi_volume && ddgi_volume.priority < selected_ddgi_volume->priority)
                {
                    continue;
                }

                Entity entity = ddgi_volume_array->index_to_entity[i];
                float3 volume_center = ddgi_volume.volume_offset;
                if (transform_array && transform_array->HasData(entity))
                {
                    const TransformComponent& transform = transform_array->GetData(entity);
                    const float3 transform_position = math::GetPosition(transform.world_transform);
                    volume_center.x += transform_position.x;
                    volume_center.y += transform_position.y;
                    volume_center.z += transform_position.z;
                }

                selected_ddgi_volume = &ddgi_volume;
                selected_ddgi_volume_entity = entity;
                selected_volume_center = volume_center;
            }

            if (selected_ddgi_volume)
            {
                const float3 probe_span = {
                    static_cast<float>((selected_ddgi_volume->probe_counts.x > 0 ? selected_ddgi_volume->probe_counts.x - 1 : 0)) * selected_ddgi_volume->probe_spacing.x,
                    static_cast<float>((selected_ddgi_volume->probe_counts.y > 0 ? selected_ddgi_volume->probe_counts.y - 1 : 0)) * selected_ddgi_volume->probe_spacing.y,
                    static_cast<float>((selected_ddgi_volume->probe_counts.z > 0 ? selected_ddgi_volume->probe_counts.z - 1 : 0)) * selected_ddgi_volume->probe_spacing.z
                };

                render_data.shader_ddgi_volume.flags = SHADER_DDGI_FLAG_ACTIVE;
                render_data.ddgi_volume_entity = selected_ddgi_volume_entity;
                render_data.shader_ddgi_volume.probe_counts = selected_ddgi_volume->probe_counts;
                render_data.shader_ddgi_volume.normal_bias = selected_ddgi_volume->normal_bias;
                render_data.shader_ddgi_volume.view_bias = selected_ddgi_volume->view_bias;
                render_data.shader_ddgi_volume.max_distance = selected_ddgi_volume->max_distance;
                render_data.shader_ddgi_volume.probe_spacing = selected_ddgi_volume->probe_spacing;
                render_data.shader_ddgi_volume.volume_min = {
                    selected_volume_center.x - probe_span.x * 0.5f,
                    selected_volume_center.y - probe_span.y * 0.5f,
                    selected_volume_center.z - probe_span.z * 0.5f
                };
            }
        }
    }
}
