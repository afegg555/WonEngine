#include "ParticleUpdateSystem.h"
#include "Scene.h"
#include "MathUtils.h"
#include "Random.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace won::ecs
{
    void ParticleUpdateSystem::Update(Scene& scene, float delta_time)
    {
        struct ParticleBucket
        {
            Vector<float4> particle_instances;
            Vector<Scene::RenderData::Sprite3DRenderable> sprite_3d_renderables;
        };

        Scene::RenderData& render_data = scene.GetRenderData();
        render_data.particle_instances.clear();

        const auto emitter_array = scene.GetComponentArray<ParticleEmitter3DComponent>().get();
        if (!emitter_array)
        {
            emitter_states.clear();
            return;
        }
        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();
        if (!transform_array || !material_array)
        {
            return;
        }

        jobsystem::Context sub_ctx;

        // Drop runtime state for entities that no longer have an emitter.
        for (auto it = emitter_states.begin(); it != emitter_states.end();)
        {
            if (!emitter_array->HasData(it->first))
            {
                it = emitter_states.erase(it);
            }
            else
            {
                ++it;
            }
        }

        const uint32 emitter_count = static_cast<uint32>(emitter_array->GetSize());
        Vector<EmitterRuntime*> emitter_runtimes(emitter_count);
        for (uint32 emitter_index = 0; emitter_index < emitter_count; ++emitter_index)
        {
            const Entity entity = emitter_array->index_to_entity[emitter_index];
            const ParticleEmitter3DComponent& emitter = emitter_array->data[emitter_index];
            EmitterRuntime& runtime = emitter_states[entity];
            if (!runtime.initialized)
            {
                runtime.rng_state = emitter.seed != 0 ? emitter.seed : won::random::GetRandomSeed();
                runtime.initialized = true;
            }
            emitter_runtimes[emitter_index] = &runtime;
        }

        Vector<ParticleBucket> particle_buckets(jobsystem::DispatchGroupCount(emitter_count, groupsize_heavy));
        jobsystem::Dispatch(sub_ctx, emitter_count, groupsize_heavy, [&](jobsystem::JobArgs args) {
            ParticleBucket& bucket = particle_buckets[args.group_id];

            const uint32 emitter_index = args.job_index;
            const Entity entity = emitter_array->index_to_entity[emitter_index];
            const ParticleEmitter3DComponent& emitter = emitter_array->data[emitter_index];
            if (!transform_array->HasData(entity) || !material_array->HasData(entity))
            {
                return;
            }
            const MaterialComponent& material = material_array->GetData(entity);
            if (material.GetMaterialSlotCount() == 0)
            {
                return;
            }

            EmitterRuntime& runtime = *emitter_runtimes[emitter_index];
            const float3 origin = math::GetPosition(transform_array->GetData(entity).world_transform);
            const float lifetime = emitter.lifetime > 0.0001f ? emitter.lifetime : 0.0001f;

            // Spawn new particles for this frame.
            if (emitter.IsActive())
            {
                runtime.spawn_accumulator += emitter.spawn_rate * delta_time;
                while (runtime.spawn_accumulator >= 1.0f)
                {
                    runtime.spawn_accumulator -= 1.0f;
                    if (runtime.particles.size() >= emitter.max_particle_count)
                    {
                        break;
                    }
                    if (!emitter.IsLooping() && runtime.total_emitted >= emitter.max_particle_count)
                    {
                        break;
                    }

                    const float angle = won::random::NextFloat(runtime.rng_state) * (2.0f * math::PI);
                    const float radius = std::sqrt(won::random::NextFloat(runtime.rng_state)) * emitter.emit_radius;
                    Particle particle = {};
                    particle.position = { origin.x + std::cos(angle) * radius, origin.y, origin.z + std::sin(angle) * radius };
                    particle.velocity = {
                        emitter.velocity.x + won::random::NextSignedFloat(runtime.rng_state) * emitter.velocity_variation,
                        emitter.velocity.y + won::random::NextSignedFloat(runtime.rng_state) * emitter.velocity_variation,
                        emitter.velocity.z + won::random::NextSignedFloat(runtime.rng_state) * emitter.velocity_variation,
                    };
                    runtime.particles.push_back(particle);
                    ++runtime.total_emitted;
                }
            }

            // Integrate motion and remove expired particles.
            for (Size particle_index = 0; particle_index < runtime.particles.size();)
            {
                Particle& particle = runtime.particles[particle_index];
                particle.age += delta_time;
                if (particle.age >= lifetime)
                {
                    runtime.particles[particle_index] = runtime.particles.back();
                    runtime.particles.pop_back();
                    continue;
                }
                particle.velocity.x += emitter.gravity.x * delta_time;
                particle.velocity.y += emitter.gravity.y * delta_time;
                particle.velocity.z += emitter.gravity.z * delta_time;
                particle.position.x += particle.velocity.x * delta_time;
                particle.position.y += particle.velocity.y * delta_time;
                particle.position.z += particle.velocity.z * delta_time;
                ++particle_index;
            }

            // Append GPU particle data and a billboard renderable per alive particle.
            const uint32 material_index = material.material->material_offset;

            for (const Particle& particle : runtime.particles)
            {
                const float t = particle.age / lifetime;
                const float size = math::Lerp(emitter.start_size, emitter.end_size, t);
                const float4 color = math::Lerp(emitter.start_color, emitter.end_color, t);

                const uint32 buffer_index = static_cast<uint32>(bucket.particle_instances.size() / 2);
                bucket.particle_instances.push_back({ particle.position.x, particle.position.y, particle.position.z, 0.0f });
                bucket.particle_instances.push_back(color);

                Scene::RenderData::Sprite3DRenderable renderable = {};
                renderable.instance_index = buffer_index;
                renderable.material_index = material_index;
                renderable.world_position = particle.position;
                renderable.size = { size, size };
                // Particles are always alpha-blended (color fades out), so flag them transparent
                // for back-to-front sorting regardless of the material.
                renderable.flags = Scene::RenderData::Sprite3DRenderable::Particle
                    | Scene::RenderData::Sprite3DRenderable::Billboard
                    | Scene::RenderData::Sprite3DRenderable::Transparent;
                const float half_extent = size * 0.5f;
                renderable.aabb.min = { particle.position.x - half_extent, particle.position.y - half_extent, particle.position.z - half_extent };
                renderable.aabb.max = { particle.position.x + half_extent, particle.position.y + half_extent, particle.position.z + half_extent };
                bucket.sprite_3d_renderables.push_back(renderable);
            }
        });
        jobsystem::Wait(sub_ctx);

        Size particle_instance_count = 0;
        Size sprite_3d_renderable_count = 0;
        for (const ParticleBucket& bucket : particle_buckets)
        {
            particle_instance_count += bucket.particle_instances.size();
            sprite_3d_renderable_count += bucket.sprite_3d_renderables.size();
        }

        render_data.particle_instances.reserve(particle_instance_count);
        render_data.sprite_3d_renderables.reserve(render_data.sprite_3d_renderables.size() + sprite_3d_renderable_count);

        uint32 particle_base_index = 0;
        for (ParticleBucket& bucket : particle_buckets)
        {
            for (Scene::RenderData::Sprite3DRenderable& renderable : bucket.sprite_3d_renderables)
            {
                renderable.instance_index += particle_base_index;
            }

            render_data.particle_instances.insert(render_data.particle_instances.end(), std::make_move_iterator(bucket.particle_instances.begin()), std::make_move_iterator(bucket.particle_instances.end()));
            render_data.sprite_3d_renderables.insert(render_data.sprite_3d_renderables.end(), std::make_move_iterator(bucket.sprite_3d_renderables.begin()), std::make_move_iterator(bucket.sprite_3d_renderables.end()));
            particle_base_index += static_cast<uint32>(bucket.particle_instances.size() / 2);
        }
    }
}
