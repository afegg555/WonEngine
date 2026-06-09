#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API Collider3DUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadMask() const override { return transform_component_mask | collider_3d_component_mask; }
        ComponentMask GetWriteMask() const override { return collider_3d_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
