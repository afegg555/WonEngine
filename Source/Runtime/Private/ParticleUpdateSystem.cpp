#include "ParticleUpdateSystem.h"
#include "Scene.h"
#include "Wind.h"
#include "MathUtils.h"
#include "Random.h"

#include <cmath>

namespace won::ecs
{
    void ParticleUpdateSystem::Update(Scene& scene, float delta_time)
    {
        const auto emitter_array = scene.GetComponentArray<ParticleEmitter3DComponent>().get();
        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        if (!emitter_array || !transform_array)
        {
            return;
        }

        const wind::WindField wind_field = wind::BuildWindField(scene);

        jobsystem::Context sub_ctx;
        const uint32 emitter_count = static_cast<uint32>(emitter_array->GetSize());
        jobsystem::Dispatch(sub_ctx, emitter_count, groupsize_heavy, [&](jobsystem::JobArgs args) {
            const Entity entity = emitter_array->index_to_entity[args.job_index];
            ParticleEmitter3DComponent& emitter = emitter_array->data[args.job_index];
            if (!transform_array->HasData(entity))
            {
                return;
            }

            if (!emitter.runtime_initialized)
            {
                emitter.rng_state = emitter.seed != 0 ? emitter.seed : won::random::GetRandomSeed();
                emitter.runtime_initialized = true;
            }

            const float3 origin = math::GetPosition(transform_array->GetData(entity).world_transform);
            const float lifetime = emitter.lifetime > 0.0001f ? emitter.lifetime : 0.0001f;

            // Spawn new particles for this frame.
            if (emitter.IsActive())
            {
                emitter.spawn_accumulator += emitter.spawn_rate * delta_time;
                while (emitter.spawn_accumulator >= 1.0f)
                {
                    emitter.spawn_accumulator -= 1.0f;
                    if (emitter.particles.size() >= emitter.max_particle_count)
                    {
                        break;
                    }
                    if (!emitter.IsLooping() && emitter.total_emitted >= emitter.max_particle_count)
                    {
                        break;
                    }

                    const float angle = won::random::NextFloat(emitter.rng_state) * (2.0f * math::PI);
                    const float radius = std::sqrt(won::random::NextFloat(emitter.rng_state)) * emitter.emit_radius;
                    ParticleEmitter3DComponent::Particle particle = {};
                    particle.position = { origin.x + std::cos(angle) * radius, origin.y, origin.z + std::sin(angle) * radius };
                    particle.velocity = {
                        emitter.velocity.x + won::random::NextSignedFloat(emitter.rng_state) * emitter.velocity_variation,
                        emitter.velocity.y + won::random::NextSignedFloat(emitter.rng_state) * emitter.velocity_variation,
                        emitter.velocity.z + won::random::NextSignedFloat(emitter.rng_state) * emitter.velocity_variation,
                    };
                    emitter.particles.push_back(particle);
                    ++emitter.total_emitted;
                }
            }

            // Integrate motion and remove expired particles.
            for (Size particle_index = 0; particle_index < emitter.particles.size();)
            {
                ParticleEmitter3DComponent::Particle& particle = emitter.particles[particle_index];
                particle.age += delta_time;
                if (particle.age >= lifetime)
                {
                    emitter.particles[particle_index] = emitter.particles.back();
                    emitter.particles.pop_back();
                    continue;
                }
                const float3 wind_velocity = wind_field.Sample(particle.position);
                particle.velocity.x += (emitter.gravity.x + wind_velocity.x * emitter.wind_influence) * delta_time;
                particle.velocity.y += (emitter.gravity.y + wind_velocity.y * emitter.wind_influence) * delta_time;
                particle.velocity.z += (emitter.gravity.z + wind_velocity.z * emitter.wind_influence) * delta_time;
                particle.position.x += particle.velocity.x * delta_time;
                particle.position.y += particle.velocity.y * delta_time;
                particle.position.z += particle.velocity.z * delta_time;
                ++particle_index;
            }
        });
        jobsystem::Wait(sub_ctx);
    }
}
