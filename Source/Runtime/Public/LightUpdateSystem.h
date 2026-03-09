#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API LightUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadMask() const override { return light_component_mask | transform_component_mask; }
        ComponentMask GetWriteMask() const override { return light_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
