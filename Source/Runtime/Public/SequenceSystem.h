#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class WONENGINE_API SequenceSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return none_component_mask; }
        ComponentMask GetWriteMask() const override { return sequence_component_mask | transform_component_mask | camera_component_mask; }
        SystemPhase GetPhase() const override { return SystemPhase::PreSimulation; }
        void Update(Scene& scene, float delta_time) override;
    };
}
