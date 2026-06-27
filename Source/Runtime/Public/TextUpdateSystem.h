#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API TextUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return transform_component_mask | material_component_mask | layer_component_mask; }
        ComponentMask GetWriteMask() const override { return text_2d_component_mask | text_3d_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
