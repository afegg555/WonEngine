#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API AnimationUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return geometry_component_mask; }
        ComponentMask GetWriteMask() const override { return animation_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
