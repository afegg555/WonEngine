#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API EnvironmentUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return environment_component_mask | ddgi_volume_component_mask | transform_component_mask; }
        ComponentMask GetWriteMask() const override { return 0; }
        void Update(Scene& scene, float delta_time) override;
    };
}
