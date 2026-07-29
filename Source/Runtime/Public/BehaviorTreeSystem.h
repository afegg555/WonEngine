#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class WONENGINE_API BehaviorTreeSystem final : public System
    {
    public:
        ComponentMask GetWriteMask() const override { return behavior_tree_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
