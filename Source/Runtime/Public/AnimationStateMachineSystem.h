#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class WONENGINE_API AnimationStateMachineSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return 0; }
        ComponentMask GetWriteMask() const override { return animation_state_machine_component_mask | animation_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
