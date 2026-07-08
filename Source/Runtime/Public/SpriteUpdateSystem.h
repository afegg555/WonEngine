#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API SpriteUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return transform_component_mask | material_component_mask | layer_component_mask | hierarchy_component_mask | rect_transform_2d_component_mask; }
        ComponentMask GetWriteMask() const override { return sprite_2d_component_mask | sprite_3d_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
