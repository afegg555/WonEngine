#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API DecalUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return decal_component_mask | transform_component_mask | material_component_mask; }
        ComponentMask GetWriteMask() const override { return 0; }
        void Update(Scene& scene, float delta_time) override;
    };
}
