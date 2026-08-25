#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API WaterSimulationSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return 0; }
        ComponentMask GetWriteMask() const override { return 0; }
        void Update(Scene& scene, float delta_time) override;
    };
}
