#include "Wind.h"
#include "Scene.h"

#include <cmath>

namespace won::wind
{
    float3 WindField::Sample(const float3& position) const
    {
        (void)position; // currently global only
        return global_velocity;
    }

    WindField BuildWindField(const ecs::Scene& scene)
    {
        WindField field = {};

        const auto environment_array = scene.GetComponentArray<ecs::EnvironmentComponent>().get();
        if (!environment_array)
        {
            return field;
        }

        for (Size i = 0; i < environment_array->GetSize(); ++i)
        {
            const ecs::EnvironmentComponent& environment = environment_array->data[i];
            if (!environment.IsActive())
            {
                continue;
            }

            const float3& direction = environment.wind_direction;
            const float length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
            if (length > 0.0001f)
            {
                const float scale = environment.wind_speed / length;
                field.global_velocity = { direction.x * scale, direction.y * scale, direction.z * scale };
            }
        }

        return field;
    }
}
