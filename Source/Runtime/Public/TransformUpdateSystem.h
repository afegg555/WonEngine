#pragma once
#include "System.h"
#include "Entity.h"
#include "Types.h"
#include "RuntimeExport.h"
#include "SceneComponents.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API TransformUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return hierarchy_component_mask | geometry_component_mask | canvas_2d_component_mask | layout_component_mask; }
        ComponentMask GetWriteMask() const override { return transform_component_mask | rect_transform_2d_component_mask; }
        SystemPhase GetPhase() const override { return SystemPhase::PreSimulation; }
        void Update(Scene& scene, float delta_time) override;

    private:
        Vector<Entity> hierarchy_update_order_cache;
        UnorderedMap<Entity, Vector<Entity>> hierarchy_children_cache;
    };
}
