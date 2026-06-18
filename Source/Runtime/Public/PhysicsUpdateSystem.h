#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API PhysicsUpdateSystem final : public System
    {
        ComponentMask GetReadMask() const override { return transform_component_mask | collider_3d_component_mask | rigidbody_3d_component_mask; }
        ComponentMask GetWriteMask() const override { return collider_3d_component_mask | rigidbody_3d_component_mask; }
        SystemExecutionPolicy GetExecutionPolicy() const override { return SystemExecutionPolicy::Synchronous; }

        void Update(Scene& scene, float delta_time) override;

    };
}
