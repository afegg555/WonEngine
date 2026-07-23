#pragma once
#include "System.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API ParticleUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return transform_component_mask; }
        ComponentMask GetWriteMask() const override { return particle_emitter_3d_component_mask; }
        void Update(Scene& scene, float delta_time) override;
    };
}
