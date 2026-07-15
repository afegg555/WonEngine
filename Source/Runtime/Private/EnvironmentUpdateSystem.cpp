#include "EnvironmentUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "JobSystem.h"
#include "Backlog.h"

namespace won::ecs
{
    void EnvironmentUpdateSystem::Update(Scene& scene, float delta_time)
    {
        auto& render_data = scene.GetRenderData();
        render_data.shader_environment.Init();
        render_data.shader_ddgi_volume.Init();
        render_data.shader_reflection_probe.Init();
        render_data.ddgi_volume_entity = INVALID_ENTITY;

        auto environment_array = scene.GetComponentArray<EnvironmentComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();

        if (environment_array)
        {
            for (Size i = 0; i < environment_array->GetSize(); ++i)
            {
                const EnvironmentComponent& environment = environment_array->data[i];
                if (!environment.IsActive())
                    continue;

                render_data.shader_environment.sky_type = static_cast<uint32>(environment.sky_type);
                if (environment.HasSky())
                {
                    render_data.shader_environment.SetSunDirection(environment.sun_direction);
                    render_data.shader_environment.SetSunColorIntensity(environment.sun_color, environment.sun_intensity);
                    render_data.shader_environment.SetSunParams(environment.sun_angular_radius, environment.sun_glow_intensity, environment.sun_glow_falloff);
                    render_data.shader_environment.SetSkyHorizonColorIntensity(environment.sky_horizon_color, environment.sky_intensity);
                    render_data.shader_environment.SetSkyZenithColorFalloff(environment.sky_zenith_color, environment.sky_horizon_falloff);
                    render_data.shader_environment.SetGroundHorizonColorIntensity(environment.ground_horizon_color, environment.ground_intensity);
                    render_data.shader_environment.SetGroundColorFalloff(environment.ground_color, environment.ground_falloff);
                }

                render_data.shader_environment.diffuse_gi_mode = static_cast<uint32>(environment.diffuse_gi_mode);
                render_data.shader_environment.SetAmbientColorIntensity(environment.ambient_color, environment.ambient_intensity);
                render_data.shader_environment.SetIndirectScale(environment.indirect_diffuse_scale, environment.indirect_specular_scale);
                break;
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
                render_data.shader_ddgi_volume.total_probe_count = selected_ddgi_volume->probe_counts.x * selected_ddgi_volume->probe_counts.y * selected_ddgi_volume->probe_counts.z;
                render_data.shader_ddgi_volume.probes_per_frame = selected_ddgi_volume->probes_per_frame;
                render_data.shader_ddgi_volume.hysteresis = selected_ddgi_volume->hysteresis;
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

        auto reflection_probe_array = scene.GetComponentArray<ReflectionProbeComponent>().get();
        if (reflection_probe_array)
        {
            for (Size i = 0; i < reflection_probe_array->GetSize(); ++i)
            {
                const ReflectionProbeComponent& probe = reflection_probe_array->data[i];
                if (!probe.IsActive())
                {
                    continue;
                }

                float3 probe_position = { 0.0f, 0.0f, 0.0f };
                const Entity entity = reflection_probe_array->index_to_entity[i];
                if (transform_array && transform_array->HasData(entity))
                {
                    probe_position = math::GetPosition(transform_array->GetData(entity).world_transform);
                }

                render_data.shader_reflection_probe.flags = SHADER_REFLECTION_PROBE_FLAG_ACTIVE;
                render_data.shader_reflection_probe.intensity = probe.intensity_multiplier;
                render_data.shader_reflection_probe.influence_radius = probe.influence_radius;
                render_data.shader_reflection_probe.position = probe_position;
                const bool has_cubemap = probe.cubemap && probe.cubemap->render_data.IsValid();
                render_data.shader_reflection_probe.cubemap_texture = has_cubemap ? probe.cubemap->render_data.srv.descriptor_index : -1;
                render_data.shader_reflection_probe.cubemap_mip_count = has_cubemap ? static_cast<float>(probe.cubemap->mip_levels) : 0.0f;
                break;
            }
        }
    }
}
