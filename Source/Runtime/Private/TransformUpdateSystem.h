#pragma once
#include "System.h"
#include "Entity.h"
#include "Types.h"
#include "RuntimeExport.h"
#include "SceneComponents.h"

namespace won::ecs
{
    class Scene;

    class TransformUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadMask() const override { return transform_component_mask | hierarchy_component_mask; }
        ComponentMask GetWriteMask() const override { return transform_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
