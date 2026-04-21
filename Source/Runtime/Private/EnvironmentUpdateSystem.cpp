#include "EnvironmentUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "SkyComponent.h"
#include "JobSystem.h"
#include "Backlog.h"

namespace won::ecs
{
    void EnvironmentUpdateSystem::Update(Scene& scene, float delta_time)
    {
        auto& render_data = scene.GetRenderData();
        render_data.shader_sky.Init();

        auto sky_array = scene.GetComponentArray<SkyComponent>().get();

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


    }
}
