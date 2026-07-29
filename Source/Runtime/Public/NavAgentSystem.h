#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class WONENGINE_API NavAgentSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return 0; }
        ComponentMask GetWriteMask() const override { return nav_agent_component_mask | transform_component_mask; }
        SystemPhase GetPhase() const override { return SystemPhase::PreSimulation; }
        void Update(Scene& scene, float delta_time) override;
    };
}
