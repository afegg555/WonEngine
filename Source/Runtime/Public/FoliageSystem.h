#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API FoliageSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return terrain_component_mask | transform_component_mask; }
        ComponentMask GetWriteMask() const override { return foliage_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
