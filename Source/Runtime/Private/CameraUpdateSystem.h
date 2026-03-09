#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class CameraUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadMask() const override { return transform_component_mask | camera_component_mask; }
        ComponentMask GetWriteMask() const override { return camera_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
