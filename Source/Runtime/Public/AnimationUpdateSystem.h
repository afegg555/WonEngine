#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API AnimationUpdateSystem final : public System
    {
    public:
        explicit AnimationUpdateSystem(bool dispatch_events_in = false)
            : dispatch_events(dispatch_events_in)
        {
        }

        ComponentMask GetReadOnlyMask() const override { return geometry_component_mask; }
        ComponentMask GetWriteMask() const override { return animation_component_mask; }
        void Update(Scene& scene, float delta_time) override;

    private:
        bool dispatch_events = false;
    };
}
