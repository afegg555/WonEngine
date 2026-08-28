#pragma once
#include "Types.h"
#include "MathTypes.h"

namespace won::ecs
{
    // CPU sprite particle emitter. Particles are simulated on the CPU and drawn through the existing Sprite3D billboard path
    // entity also needs a MaterialComponent (texture)
    struct ParticleEmitter3DComponent
    {
        struct Particle
        {
            float3 position = {};
            float3 velocity = {};
            float age = 0.0f;
        };

        enum Flags
        {
            Empty = 0,
            Active = 1 << 0,
            Looping = 1 << 1,
        };

        uint32 flags = Active | Looping;

        uint32 max_particle_count = 256; // hard cap; keep modest (per-particle draw call until instancing)
        float spawn_rate = 32.0f; // particles per second
        float lifetime = 2.0f; // seconds each particle lives

        float start_size = 1.0f; // billboard size at birth
        float end_size = 0.2f; // billboard size at death

        float4 start_color = { 1.0f, 1.0f, 1.0f, 1.0f }; // RGBA at birth
        float4 end_color = { 1.0f, 1.0f, 1.0f, 0.0f }; // RGBA at death (alpha 0 = fade out)

        float3 velocity = { 0.0f, 3.0f, 0.0f }; // base spawn velocity (world space)
        float velocity_variation = 1.5f; // random +/- added per axis at spawn
        float3 gravity = { 0.0f, -1.0f, 0.0f }; // constant acceleration
        float wind_influence = 1.0f;
        float emit_radius = 0.5f; // spawn disc radius around the emitter origin

        uint32 seed = 0; // deterministic per-emitter randomization

        // runtime state, not serialized
        Vector<Particle> particles;
        float spawn_accumulator = 0.0f;
        uint32 total_emitted = 0;
        uint32 rng_state = 0;
        bool runtime_initialized = false;

        constexpr void SetActive(bool value = true) { if (value) { flags |= Active; } else { flags &= ~Active; } }
        constexpr bool IsActive() const { return (flags & Active) != 0; }
        constexpr bool IsLooping() const { return (flags & Looping) != 0; }
    };
}
