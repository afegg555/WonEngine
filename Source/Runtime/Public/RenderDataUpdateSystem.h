#pragma once
#include "System.h"
#include "Entity.h"
#include "Types.h"
#include "RuntimeExport.h"
#include "SceneComponents.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API RenderDataUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadMask() const override { return transform_component_mask | geometry_component_mask | material_component_mask; }
        ComponentMask GetWriteMask() const override { return none_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
