#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API MeshUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return transform_component_mask | terrain_component_mask; }
        ComponentMask GetWriteMask() const override { return geometry_component_mask; }
        SystemPhase GetPhase() const override { return SystemPhase::PostSimulation; }

        void Update(Scene& scene, float delta_time) override;
    };
}
