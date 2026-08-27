#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API PhysicsUpdateSystem final : public System
    {
        ComponentMask GetReadOnlyMask() const override { return collision_layer_component_mask | hierarchy_component_mask | geometry_component_mask; }
        ComponentMask GetWriteMask() const override { return transform_component_mask | collider_3d_component_mask | rigidbody_3d_component_mask | joint_component_mask | soft_body_component_mask; }
        SystemExecutionPolicy GetExecutionPolicy() const override { return SystemExecutionPolicy::Synchronous; }
        SystemPhase GetPhase() const override { return SystemPhase::Simulation; }

        void Update(Scene& scene, float delta_time) override;

    };
}
