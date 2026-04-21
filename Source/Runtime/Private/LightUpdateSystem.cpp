#include "LightUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "LightComponent.h"
#include "JobSystem.h"
#include "Backlog.h"

namespace won::ecs
{
    void LightUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto light_array = scene.GetComponentArray<LightComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        Scene::RenderData& render_data = scene.GetRenderData();

        Size light_count = light_array->GetSize();
        if (light_array->GetSize() > NUM_MAX_LIGHTS_FORWARD_RENDERING)
        {
            backlog::Post(String("Maximum number of lights in forward rendering is: ") + std::to_string(NUM_MAX_LIGHTS_FORWARD_RENDERING), backlog::LogLevel::Warning);
            light_count = NUM_MAX_LIGHTS_FORWARD_RENDERING;
        }

        render_data.shader_lights.resize(light_count);
        render_data.forward_light_mask = { 0,0,0,0 };

        jobsystem::Dispatch(sub_ctx, (uint32)light_count, groupsize, [&](jobsystem::JobArgs args) {
            LightComponent& light = light_array->data[args.job_index];
            Entity entity = light_array->index_to_entity[args.job_index];

            if (!transform_array->HasData(entity))
            {
                backlog::Post("Light Entity does not have transform component", backlog::LogLevel::Error);
                return;
            }

            const TransformComponent& transform = transform_array->GetData(entity);

            light.position = math::GetPosition(transform.world_transform);
            light.direction = math::GetForward(transform.world_transform);

            switch (light.type)
            {
            case LightComponent::Directional:
                light.aabb.CreateFromHalfWidth(float3(0, 0, 0), float3(FLT_MAX, FLT_MAX, FLT_MAX));
                break;
            case LightComponent::Spot:
                light.aabb.CreateFromHalfWidth(light.position, float3(light.range, light.range, light.range));
                break;
            case LightComponent::Point:
                light.aabb.CreateFromHalfWidth(light.position, float3(light.range, light.range, light.range));
                break;
            default:
                light.aabb.CreateFromHalfWidth(float3(0, 0, 0), float3(0, 0, 0));
                break;
            }

            if (light.IsActive())
            {
                ShaderLight& shader_light = render_data.shader_lights[args.job_index];
                shader_light.Init();
                shader_light.position = light.position;

                shader_light.SetType(light.type);
                shader_light.SetColor({ light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity, light.intensity });
                shader_light.SetRange(light.range);
                shader_light.SetDirection(light.direction);
                shader_light.SetOuterConeAngleCos(cos(light.outer_cone_angle));
                shader_light.SetInnerConeAngleCos(cos(light.inner_cone_angle));
                if (!light.IsDynamic()) shader_light.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_LIGHT_STATIC);
                if (light.IsCastShadow()) shader_light.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_LIGHT_CASTING_SHADOW);

                const uint8_t bucket_index = uint8_t(args.job_index / 32);
                const uint8_t bucket_place = uint8_t(args.job_index % 32);
                uint32_t* value = reinterpret_cast<uint32_t*>(&render_data.forward_light_mask);
                value[bucket_index] |= 1 << bucket_place;
            }
        });

        jobsystem::Wait(sub_ctx);
    }
}
