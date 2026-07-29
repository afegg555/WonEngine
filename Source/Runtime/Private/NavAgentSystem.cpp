#include "NavAgentSystem.h"

#include "JobSystem.h"
#include "Scene.h"

namespace won::ecs
{
    void NavAgentSystem::Update(Scene& scene, float delta_time)
    {
        auto agent_array = scene.GetComponentArray<NavAgentComponent>();
        auto transform_array = scene.GetComponentArray<TransformComponent>();
        if (!agent_array || !transform_array)
        {
            return;
        }

        jobsystem::Context ctx;
        jobsystem::Dispatch(ctx, (uint32_t)agent_array->GetSize(), jobsystem::groupsize_light, [&](jobsystem::JobArgs args)
        {
            NavAgentComponent& agent = agent_array->data[args.job_index];
            if (!agent.IsEnabled() || agent.state != NavAgentComponent::MoveState::Moving)
            {
                return;
            }

            const Entity entity = agent_array->index_to_entity[args.job_index];
            if (!transform_array->HasData(entity))
            {
                return;
            }
            TransformComponent& transform = transform_array->GetData(entity);

            float remaining = delta_time * agent.move_speed;
            float move_dir_x = 0.0f;
            float move_dir_z = 0.0f;
            bool moved = false;

            while (remaining > 0.0f)
            {
                if (agent.path_index < 0 || static_cast<Size>(agent.path_index) >= agent.path.size())
                {
                    agent.state = NavAgentComponent::MoveState::Arrived;
                    agent.path.clear();
                    agent.path_index = 0;
                    break;
                }

                const float3& waypoint = agent.path[static_cast<Size>(agent.path_index)];
                const float to_x = waypoint.x - transform.position.x;
                const float to_z = waypoint.z - transform.position.z;
                const float distance = std::sqrt(to_x * to_x + to_z * to_z);

                if (distance <= agent.arrival_threshold)
                {
                    transform.position.y = waypoint.y;
                    transform.SetDirty();
                    ++agent.path_index;
                    continue;
                }

                const float step = std::min(remaining, distance);
                const float inv_distance = 1.0f / distance;
                move_dir_x = to_x * inv_distance;
                move_dir_z = to_z * inv_distance;
                moved = true;

                transform.position.x += move_dir_x * step;
                transform.position.z += move_dir_z * step;
                transform.position.y += (waypoint.y - transform.position.y) * std::min(1.0f, step * inv_distance);
                transform.SetDirty();

                remaining -= step;
            }

            if (moved && agent.RotatesToMovement())
            {
                const float target_yaw = std::atan2(move_dir_x, move_dir_z) + math::PI;
                float delta_yaw = std::fmod(target_yaw - agent.yaw, math::PI * 2.0f);
                if (delta_yaw > math::PI)
                {
                    delta_yaw -= math::PI * 2.0f;
                }
                else if (delta_yaw < -math::PI)
                {
                    delta_yaw += math::PI * 2.0f;
                }
                const float max_step = agent.turn_rate * delta_time;
                agent.yaw += std::clamp(delta_yaw, -max_step, max_step);
                transform.SetRotationEuler(0.0f, agent.yaw, 0.0f);
            }
        });
        jobsystem::Wait(ctx);
    }
}
