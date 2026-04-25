#pragma once
#include "ComponentManager.h"
#include "Entity.h"
#include "System.h"
#include "SceneComponents.h"
#include "TransformUpdateSystem.h"
#include "EnvironmentUpdateSystem.h"
#include "CameraUpdateSystem.h"
#include "LightUpdateSystem.h"
#include "GeometryUpdateSystem.h"
#include "MaterialUpdateSystem.h"
#include "RenderableUpdateSystem.h"
#include "ShaderInterop_Renderer.h"
#include "BVH.h"

#include "Types.h"
#include "MathUtils.h"
#include "Profiler.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace won::rendering
{
    class RHIResource;
}

namespace won::ecs
{
    struct RayCastHit
    {
        Entity entity = INVALID_ENTITY;
        uint32 triangle_index = 0;
        float distance = (std::numeric_limits<float>::max)();
        float2 barycentric = {};
    };

    class Scene
    {
    public:
        Scene()
        {
            component_manager.RegisterComponent<TransformComponent>();
            component_manager.RegisterComponent<HierarchyComponent>();
            component_manager.RegisterComponent<NameComponent>();
            component_manager.RegisterComponent<GeometryComponent>();
            component_manager.RegisterComponent<MaterialComponent>();
            component_manager.RegisterComponent<CameraComponent>();
            component_manager.RegisterComponent<LightComponent>();
            component_manager.RegisterComponent<SkyComponent>();
            component_manager.RegisterComponent<FogVolumeComponent>();
            component_manager.RegisterComponent<EnvironmentLightingComponent>();
            component_manager.RegisterComponent<DDGIVolumeComponent>();

            AddSystem(std::make_shared<TransformUpdateSystem>());
            AddSystem(std::make_shared<EnvironmentUpdateSystem>());
            AddSystem(std::make_shared<CameraUpdateSystem>());
            AddSystem(std::make_shared<LightUpdateSystem>());
            AddSystem(std::make_shared<GeometryUpdateSystem>());
            AddSystem(std::make_shared<MaterialUpdateSystem>());
            AddSystem(std::make_shared<RenderableUpdateSystem>());
        }

        Entity CreateEntity()
        {
            Entity entity = ecs::CreateEntity();
            entities.push_back(entity);
            return entity;
        }

        void DestroyEntity(Entity entity)
        {
            component_manager.EntityDestroyed(entity);
            entities.erase(
                std::remove_if(
                    entities.begin(),
                    entities.end(),
                    [entity](const Entity& current)
                    {
                        return current == entity;
                    }),
                entities.end());
        }

        template <typename Component, typename... Args>
        Component* AddComponent(Entity entity, Args&&... args)
        {
            Component component { std::forward<Args>(args)... };
            return component_manager.AddComponent<Component>(entity, component);
        }

        template <typename Component>
        Component* GetComponent(Entity entity)
        {
            return component_manager.GetComponent<Component>(entity);
        }

        template <typename Component>
        void RemoveComponent(Entity entity)
        {
            component_manager.RemoveComponent<Component>(entity);
        }

        template <typename Component>
        bool HasComponent(Entity entity) const
        {
            return component_manager.HasComponent<Component>(entity);
        }

        template <typename Component>
        std::shared_ptr<ComponentArray<Component>> GetComponentArray()
        {
            return component_manager.GetComponentArray<Component>();
        }

        void AddSystem(const std::shared_ptr<System>& system)
        {
            if (system)
            {
                systems.push_back(system);
                system_schedule_dirty = true;
            }
        }

        void Update(float delta_time)
        {
            if (system_schedule_dirty)
            {
                system_execution_batches.clear();
                system_execution_batches.reserve(systems.size());

                const uint32 system_count = static_cast<uint32>(systems.size());

                Vector<ComponentMask> read_masks(system_count);
                Vector<ComponentMask> write_masks(system_count);

                uint32 active_system_count = 0;
                for (uint32 i = 0; i < system_count; ++i)
                {
                    if (!systems[i])
                    {
                        continue;
                    }

                    ++active_system_count;
                    read_masks[i] = systems[i]->GetReadMask();
                    write_masks[i] = systems[i]->GetWriteMask();
                }

                Vector<Vector<uint32>> graph(system_count);
                Vector<uint32> indegree(system_count, 0);

                auto add_edge = [&](uint32 from, uint32 to)
                    {
                        if (from == to)
                        {
                            return;
                        }
                        graph[from].push_back(to);
                        ++indegree[to];
                    };

                for (uint32 i = 0; i < system_count; ++i)
                {
                    if (!systems[i])
                    {
                        continue;
                    }

                    for (uint32 j = i + 1; j < system_count; ++j)
                    {
                        if (!systems[j])
                        {
                            continue;
                        }

                        const ComponentMask read_i = read_masks[i];
                        const ComponentMask write_i = write_masks[i];
                        const ComponentMask read_j = read_masks[j];
                        const ComponentMask write_j = write_masks[j];

                        const bool raw_i_to_j = (write_i & read_j) != 0;
                        const bool raw_j_to_i = (write_j & read_i) != 0;
                        const bool waw = (write_i & write_j) != 0;

                        if (raw_i_to_j)
                        {
                            add_edge(i, j);
                        }
                        if (raw_j_to_i)
                        {
                            add_edge(j, i);
                        }
                        if (waw)
                        {
                            add_edge(i, j); // deterministic for i < j
                        }
                    }
                }

                Vector<uint8> processed(system_count, 0);

                auto build_ready = [&]()
                    {
                        Vector<uint32> ready;
                        ready.reserve(system_count);
                        for (uint32 i = 0; i < system_count; ++i)
                        {
                            if (systems[i] && !processed[i] && indegree[i] == 0)
                            {
                                ready.push_back(i);
                            }
                        }
                        return ready;
                    };

                uint32 processed_count = 0;
                bool had_cycle = false;

                while (processed_count < active_system_count)
                {
                    Vector<uint32> batch = build_ready();

                    if (batch.empty())
                    {
                        had_cycle = true;

                        // Force progress: pick first unprocessed system and break its incoming edges.
                        uint32 forced = UINT32_MAX;
                        for (uint32 i = 0; i < system_count; ++i)
                        {
                            if (systems[i] && !processed[i])
                            {
                                forced = i;
                                break;
                            }
                        }

                        if (forced == UINT32_MAX)
                        {
                            break;
                        }

                        indegree[forced] = 0;
                        batch.push_back(forced);
                    }

                    system_execution_batches.push_back(batch);

                    for (uint32 s : batch)
                    {
                        if (!systems[s] || processed[s])
                        {
                            continue;
                        }

                        processed[s] = 1;
                        ++processed_count;

                        for (uint32 to : graph[s])
                        {
                            if (indegree[to] > 0)
                            {
                                --indegree[to];
                            }
                        }
                    }
                }
                assert(had_cycle == false);
                system_schedule_dirty = false;
            }

            for (const Vector<uint32>& batch : system_execution_batches)
            {
                if (batch.empty())
                {
                    continue;
                }

                jobsystem::Context ctx;
                ctx.priority = Priority::High;
                for (uint32 system_index : batch)
                {
                    if (system_index >= systems.size())
                    {
                        continue;
                    }

                    const std::shared_ptr<System>& system = systems[system_index];
                    if (system)
                    {
                        jobsystem::Execute(ctx, [&](jobsystem::JobArgs args) { system->Update(*this, delta_time); });
                    }
                }
                jobsystem::Wait(ctx);
            }

            if (cpu_bvh_dirty)
            {
                BuildBVH();
            }

            //if (gpu_bvh_dirty)
            //{
            //    BuildGPUBVH();
            //}
        }

        const Vector<Entity>& GetEntities() const
        {
            return entities;
        }

        void SetBVHDirty(bool value = true)
        {
            cpu_bvh_dirty = value;
            gpu_bvh_dirty = value;
        }

        void BuildBVH()
        {
            auto geometry_array = GetComponentArray<GeometryComponent>().get();
            auto transform_array = GetComponentArray<TransformComponent>().get();
            if (!geometry_array || !transform_array)
            {
                scene_bvh.Clear();
                scene_bvh_entities.clear();
                cpu_bvh_dirty = false;
                return;
            }
            profiler::ScopedRangeCPU range("Scene::BuildBVH");

            Vector<math::bvh::BVHPrimitive> primitives;
            primitives.reserve(geometry_array->GetSize());
            scene_bvh_entities.clear();
            scene_bvh_entities.reserve(geometry_array->GetSize());

            for (Size i = 0; i < geometry_array->GetSize(); ++i)
            {
                Entity entity = geometry_array->index_to_entity[i];
                const GeometryComponent& geometry = geometry_array->data[i];
                if (!geometry.mesh || !geometry.mesh->IsValid() || !transform_array->HasData(entity))
                {
                    continue;
                }

                const TransformComponent& transform = transform_array->GetData(entity);
                if (!transform.world_bounds.IsValid())
                {
                    continue;
                }

                primitives.push_back(math::bvh::MakePrimitive(transform.world_bounds, static_cast<uint32>(scene_bvh_entities.size())));
                scene_bvh_entities.push_back(entity);
            }

            scene_bvh.Build(primitives);
            cpu_bvh_dirty = false;
        }

        bool RayCastClosest(const math::Ray& ray, RayCastHit& out_hit, bool use_local_bvh = true)
        {
            out_hit = {};

            if (scene_bvh_dirty)
            {
                BuildBVH();
            }

            if (!scene_bvh.IsValid())
            {
                return false;
            }

            math::bvh::BVHRayHit bvh_hit = {};
            const bool hit = math::bvh::IntersectClosest(scene_bvh, ray, 0.0f, (std::numeric_limits<float>::max)(),
                [&](const math::bvh::BVHPrimitive& primitive, float min_distance, float max_distance, float& out_distance)
                {
                    if (primitive.user_data >= scene_bvh_entities.size())
                    {
                        return false;
                    }

                    const Entity entity = scene_bvh_entities[primitive.user_data];
                    const GeometryComponent* geometry = GetComponent<GeometryComponent>(entity);
                    const TransformComponent* transform = GetComponent<TransformComponent>(entity);
                    if (!geometry || !transform || !geometry->mesh)
                    {
                        return false;
                    }

                    bool found_hit = false;
                    float closest_distance = max_distance;
                    float2 closest_barycentric = {};
                    uint32 closest_triangle_index = 0;
                    const XMMATRIX world_transform = transform->GetWorldTransform();
                    const XMMATRIX inv_world_transform = XMMatrixInverse(nullptr, world_transform);
                    const XMVECTOR ray_origin = XMLoadFloat3(&ray.origin);
                    const XMVECTOR ray_direction = XMVector3Normalize(XMLoadFloat3(&ray.direction));

                    resource::Mesh& mesh = *geometry->mesh;
                    math::Ray local_ray = {};
                    XMStoreFloat3(&local_ray.origin, XMVector3TransformCoord(ray_origin, inv_world_transform));
                    XMStoreFloat3(&local_ray.direction, XMVector3Normalize(XMVector3TransformNormal(ray_direction, inv_world_transform)));
                    const XMVECTOR local_ray_origin = XMLoadFloat3(&local_ray.origin);
                    const XMVECTOR local_ray_direction = XMLoadFloat3(&local_ray.direction);

                    auto test_local_triangle = [&](uint32 triangle_index) -> bool
                    {
                        const uint32 index = triangle_index * 3;
                        if (index + 2 >= mesh.indices.size())
                        {
                            return false;
                        }

                        const uint32 i0 = mesh.indices[index];
                        const uint32 i1 = mesh.indices[index + 1];
                        const uint32 i2 = mesh.indices[index + 2];
                        if (i0 >= mesh.positions.size() || i1 >= mesh.positions.size() || i2 >= mesh.positions.size())
                        {
                            return false;
                        }

                        const XMVECTOR v0 = XMLoadFloat3(&mesh.positions[i0]);
                        const XMVECTOR v1 = XMLoadFloat3(&mesh.positions[i1]);
                        const XMVECTOR v2 = XMLoadFloat3(&mesh.positions[i2]);

                        float local_distance = 0.0f;
                        float2 barycentric = {};
                        if (!math::RayTriangleIntersects(local_ray_origin, local_ray_direction, v0, v1, v2, local_distance, barycentric, 0.0f))
                        {
                            return false;
                        }

                        const XMVECTOR local_hit_position = v0 + (v1 - v0) * barycentric.x + (v2 - v0) * barycentric.y;
                        const XMVECTOR world_hit_position = XMVector3TransformCoord(local_hit_position, world_transform);
                        const float world_distance = XMVectorGetX(XMVector3Length(world_hit_position - ray_origin));
                        if (world_distance < min_distance || world_distance > closest_distance)
                        {
                            return false;
                        }

                        found_hit = true;
                        closest_distance = world_distance;
                        closest_barycentric = barycentric;
                        closest_triangle_index = triangle_index;
                        return true;
                    };

                    bool used_local_bvh = false;
                    if (use_local_bvh)
                    {
                        if (!mesh.local_bvh.IsValid())
                        {
                            mesh.BuildBVH();
                        }

                        if (mesh.local_bvh.IsValid())
                        {
                            used_local_bvh = true;
                            math::bvh::BVHRayHit local_bvh_hit = {};
                            math::bvh::IntersectClosest(mesh.local_bvh, local_ray, 0.0f, (std::numeric_limits<float>::max)(),
                                [&](const math::bvh::BVHPrimitive& local_primitive, float local_min_distance, float local_max_distance, float& out_local_distance)
                                {
                                    float local_bounds_distance = 0.0f;
                                    if (!local_primitive.bounds.IntersectAABB(local_ray, local_min_distance, local_max_distance, local_bounds_distance))
                                    {
                                        return false;
                                    }

                                    if (!test_local_triangle(local_primitive.user_data))
                                    {
                                        return false;
                                    }

                                    out_local_distance = local_max_distance;
                                    return true;
                                }, local_bvh_hit);
                        }
                    }

                    if (!used_local_bvh)
                    {
                        for (uint32 index = 0; index + 2 < mesh.indices.size(); index += 3)
                        {
                            test_local_triangle(index / 3);
                        }
                    }

                    if (!found_hit)
                    {
                        return false;
                    }

                    out_distance = closest_distance;
                    out_hit.entity = entity;
                    out_hit.triangle_index = closest_triangle_index;
                    out_hit.barycentric = closest_barycentric;
                    return true;
                }, bvh_hit);

            if (!hit)
            {
                out_hit = {};
                return false;
            }

            out_hit.distance = bvh_hit.distance;
            return true;
        }

        struct RenderData
        {
            struct Renderable
            {
                enum Flags : uint32
                {
                    None = 0,
                    CastShadow = 1 << 0,
                    Transparent = 1 << 1,
                };

                ObjectPushConstants push_constants;
                std::shared_ptr<rendering::RHIResource> index_buffer;
                uint32 index_offset = 0;
                uint32 index_count = 0;
                uint32 flags = None;

                bool IsTransparent() const
                {
                    return (flags & Transparent) != 0;
                }

                bool IsCastShadow() const
                {
                    return (flags & CastShadow) != 0;
                }
            };

            struct RenderShadowSlice
            {
                uint32 light_index = 0;
                float4x4 view_projection = math::IDENTITY_MATRIX;
                int4 shadow_map_atlas_rect = { -1, -1, 0, 0 };

                bool HasShadowMapAtlasRect() const { return shadow_map_atlas_rect.z > 0 && shadow_map_atlas_rect.w > 0; }
            };

            Vector<ShaderInstance> shader_instances;
            Vector<ShaderGeometry> shader_geometries;
            Vector<ShaderMaterial> shader_materials;
            Vector<Renderable> renderables;

            Vector<ShaderLight> shader_lights; // all lights
            Vector<ShaderShadowCascade> shader_shadow_cascades; // lights with shadow map
            Vector<RenderShadowSlice> render_shadow_slices;
            uint4 forward_light_mask;
            uint2 shadow_map_atlas_size = { 0, 0 };
            math::AABB shadow_caster_world_bound;

            ShaderSky shader_sky;
            ShaderEnvironmentLighting shader_environment_lighting;
            ShaderDDGIVolume shader_ddgi_volume;
            Entity ddgi_volume_entity = INVALID_ENTITY;

            void Clear()
            {
                shader_instances.clear();
                shader_geometries.clear();
                shader_materials.clear();
                renderables.clear();

                shader_lights.clear();
                shader_shadow_cascades.clear();
                render_shadow_slices.clear();
                forward_light_mask = { 0,0,0,0 };
                shadow_map_atlas_size = { 0, 0 };
                shadow_caster_world_bound.Invalidate();
                shader_sky.Init();
                shader_environment_lighting.Init();
                shader_ddgi_volume.Init();
                ddgi_volume_entity = INVALID_ENTITY;
            }
        };

        RenderData& GetRenderData()
        {
            return render_data;
        }

    private:

        RenderData render_data;
        ComponentManager component_manager;
        Vector<Entity> entities;
        math::bvh::BVH scene_bvh;
        Vector<Entity> scene_bvh_entities;
        Vector<std::shared_ptr<System>> systems;
        Vector<Vector<uint32>> system_execution_batches;
        bool cpu_bvh_dirty = true;
        bool gpu_bvh_dirty = true;
        bool system_schedule_dirty = true;
    };
}
