#pragma once
#include "System.h"
#include "RuntimeExport.h"
#include "Types.h"
#include "Entity.h"
#include "MathTypes.h"

namespace won::ecs
{
    class Scene;

    class WONENGINE_API ParticleUpdateSystem final : public System
    {
    public:
        ComponentMask GetReadOnlyMask() const override { return transform_component_mask | material_component_mask | particle_emitter_3d_component_mask; }
        ComponentMask GetWriteMask() const override { return sprite_3d_component_mask; }
        void Update(Scene& scene, float delta_time) override;

    private:
        struct Particle
        {
            float3 position = {};
            float3 velocity = {};
            float age = 0.0f;
        };

        struct EmitterRuntime
        {
            Vector<Particle> particles;
            float spawn_accumulator = 0.0f;
            uint32 total_emitted = 0;
            uint32 rng_state = 0;
            bool initialized = false;
        };

        // Per-emitter runtime state, persisted across frames (not serialized).
        UnorderedMap<Entity, EmitterRuntime> emitter_states;
    };
}
