#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API EnvironmentUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadMask() const override { return sky_component_mask | environment_lighting_component_mask; }
        ComponentMask GetWriteMask() const override { return 0; }
        void Update(Scene& scene, float delta_time) override;
    };
}
