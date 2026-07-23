#include "LightUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "JobSystem.h"
#include "Backlog.h"

#include <atomic>

namespace won::ecs
{
    void LightUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto light_array = scene.GetComponentArray<LightComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();

        const uint32 count = static_cast<uint32>(light_array->GetSize());
        std::atomic<bool> any_dirty = false;
        jobsystem::Dispatch(sub_ctx, count, jobsystem::groupsize_light, [&](jobsystem::JobArgs args)
        {
            LightComponent& light = light_array->data[args.job_index];
            Entity entity = light_array->index_to_entity[args.job_index];

            if (!transform_array->HasData(entity))
            {
                backlog::Post("Light Entity does not have transform component", backlog::LogLevel::Error);
                return;
            }

            const TransformComponent& transform = transform_array->GetData(entity);
            const float3 new_position = math::GetPosition(transform.world_transform);
            const float3 new_direction = math::GetForward(transform.world_transform);

            const bool moved = new_position.x != light.position.x || new_position.y != light.position.y || new_position.z != light.position.z
                || new_direction.x != light.direction.x || new_direction.y != light.direction.y || new_direction.z != light.direction.z;
            if (moved)
            {
                any_dirty.store(true, std::memory_order_relaxed);
            }

            light.position = new_position;
            light.direction = new_direction;

            switch (light.type)
            {
            case LightComponent::LightType::Directional:
                light.aabb.CreateFromHalfWidth(float3(0, 0, 0), float3(FLT_MAX, FLT_MAX, FLT_MAX));
                break;
            case LightComponent::LightType::Spot:
                light.aabb.CreateFromHalfWidth(light.position, float3(light.range, light.range, light.range));
                break;
            case LightComponent::LightType::Point:
                light.aabb.CreateFromHalfWidth(light.position, float3(light.range, light.range, light.range));
                break;
            default:
                light.aabb.CreateFromHalfWidth(float3(0, 0, 0), float3(0, 0, 0));
                break;
            }
        });
        jobsystem::Wait(sub_ctx);

        if (any_dirty.load(std::memory_order_relaxed))
        {
            scene.MarkGpuDirty(light_component_mask);
        }
    }
}
