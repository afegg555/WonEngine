#pragma once
#include "ComponentManager.h"
#include "Entity.h"
#include "System.h"
#include "SceneComponents.h"
#include "BuiltinTypeMeta.h"
#include "Systems.h"
#include "PhysicsWorld.h"
#include "ShaderInterop_Renderer.h"
#include "BVH.h"
#include "RenderingUtils.h"

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
    struct SceneDesc
    {
        script::ScriptRuntime* script_runtime = nullptr; // Non-owning.
        won::physics::PhysicsWorldDesc physics;
    };

    struct RayCastHit
    {
        Entity entity = INVALID_ENTITY;
        float distance = (std::numeric_limits<float>::max)();
    };

    struct RayCastBVHHit
    {
        RayCastHit hit = {};
        uint32 triangle_index = UINT32_MAX;
        float2 barycentric = {};
    };

    struct OverlapHit
    {
        Entity entity = INVALID_ENTITY;
    };



    class Scene
    {
    public:
        Scene(const SceneDesc& desc = {})
        {
            component_manager.RegisterComponent<TransformComponent>();
            component_manager.RegisterComponent<HierarchyComponent>();
            component_manager.RegisterComponent<NameComponent>();
            component_manager.RegisterComponent<GeometryComponent>();
            component_manager.RegisterComponent<MaterialComponent>();
            component_manager.RegisterComponent<Sprite2DComponent>();
            component_manager.RegisterComponent<Sprite3DComponent>();
            component_manager.RegisterComponent<Text2DComponent>();
            component_manager.RegisterComponent<Text3DComponent>();
            component_manager.RegisterComponent<CameraComponent>();
            component_manager.RegisterComponent<LightComponent>();
            component_manager.RegisterComponent<SkyComponent>();
            component_manager.RegisterComponent<FogVolumeComponent>();
            component_manager.RegisterComponent<EnvironmentLightingComponent>();
            component_manager.RegisterComponent<DDGIVolumeComponent>();
            component_manager.RegisterComponent<AnimationComponent>();
            component_manager.RegisterComponent<ScriptComponent>();
            component_manager.RegisterComponent<Collider3DComponent>();
            component_manager.RegisterComponent<Rigidbody3DComponent>();

            if (desc.script_runtime)
            {
                AddSystem(std::make_shared<ScriptUpdateSystem>(desc.script_runtime));
            }
            AddSystem(std::make_shared<TransformUpdateSystem>());
            AddSystem(std::make_shared<PhysicsUpdateSystem>());
            if (desc.script_runtime)
            {
                AddSystem(std::make_shared<ScriptEventDispatchSystem>(desc.script_runtime));
            }
            AddSystem(std::make_shared<EnvironmentUpdateSystem>());
            AddSystem(std::make_shared<CameraUpdateSystem>());
            AddSystem(std::make_shared<LightUpdateSystem>());
            AddSystem(std::make_shared<GeometryUpdateSystem>());
            AddSystem(std::make_shared<MaterialUpdateSystem>());
            AddSystem(std::make_shared<AnimationUpdateSystem>());
            AddSystem(std::make_shared<RenderableUpdateSystem>());
            AddSystem(std::make_shared<SpriteUpdateSystem>());
            AddSystem(std::make_shared<TextUpdateSystem>());

            physics_world = std::make_unique<won::physics::PhysicsWorld>(desc.physics);
        }

        Entity CreateEntity()
        {
            Entity entity = next_entity++;
            entities.push_back(entity);
            return entity;
        }

        void DestroyEntity(Entity entity)
        {
            if (entity == INVALID_ENTITY)
            {
                return;
            }

            Vector<Entity> entities_to_destroy;
            entities_to_destroy.push_back(entity);

            auto hierarchy_array = GetComponentArray<HierarchyComponent>();
            if (hierarchy_array)
            {
                for (Size destroy_index = 0; destroy_index < entities_to_destroy.size(); ++destroy_index)
                {
                    const Entity parent = entities_to_destroy[destroy_index];
                    for (Size hierarchy_index = 0; hierarchy_index < hierarchy_array->data.size(); ++hierarchy_index)
                    {
                        if (hierarchy_array->data[hierarchy_index].parent_id != parent)
                        {
                            continue;
                        }

                        const Entity child = hierarchy_array->index_to_entity[hierarchy_index];
                        if (std::find(entities_to_destroy.begin(), entities_to_destroy.end(), child) == entities_to_destroy.end())
                        {
                            entities_to_destroy.push_back(child);
                        }
                    }
                }
            }

            for (Entity current : entities_to_destroy)
            {
                component_manager.RemoveComponents(current);
            }

            entities.erase(
                std::remove_if(
                    entities.begin(),
                    entities.end(),
                    [&entities_to_destroy](const Entity& current)
                    {
                        return std::find(entities_to_destroy.begin(), entities_to_destroy.end(), current) != entities_to_destroy.end();
                    }),
                entities.end());
            SetBVHDirty();
        }

        void ClearEntities()
        {
            component_manager.Clear();
            entities.clear();
            render_data.Clear();
            scene_bvh.Clear();
            scene_bvh_entities.clear();
            physics_world->Clear();
            next_entity = INVALID_ENTITY + 1;
            SetBVHDirty();
        }

        template <typename Component, typename... Args>
        Component* AddComponent(Entity entity, Args&&... args)
        {
            Component component { std::forward<Args>(args)... };
            Component* result = component_manager.AddComponent<Component>(entity, component);

            if constexpr (std::is_same_v<Component, HierarchyComponent>)
            {
                SetHierarchyTopologyDirty(true);
                if (HasComponent<TransformComponent>(entity))
                {
                    GetComponent<TransformComponent>(entity)->SetDirty(true);
                }
            }
            return result;
        }

        void RegisterComponent(const won::TypeDesc* type_desc)
        {
            component_manager.RegisterComponent(type_desc);
        }

        void* AddComponent(Entity entity, won::TypeId type_id, const void* component)
        {
            void* result = component_manager.AddComponent(entity, type_id, component);

            const won::TypeDesc* hierarchy_desc = reflection::TypeMeta<HierarchyComponent>::Get();
            if (hierarchy_desc && type_id == hierarchy_desc->type_id)
            {
                SetHierarchyTopologyDirty(true);
                if (HasComponent<TransformComponent>(entity))
                {
                    GetComponent<TransformComponent>(entity)->SetDirty(true);
                }
            }
            return result;
        }

        void* AddComponent(Entity entity, const won::TypeDesc* type_desc)
        {
            if (!type_desc)
            {
                return nullptr;
            }
            component_manager.RegisterComponent(type_desc);
            void* result = component_manager.AddComponent(entity, type_desc->type_id, nullptr);

            const won::TypeDesc* hierarchy_desc = reflection::TypeMeta<HierarchyComponent>::Get();
            if (hierarchy_desc && type_desc->type_id == hierarchy_desc->type_id)
            {
                SetHierarchyTopologyDirty(true);
                if (HasComponent<TransformComponent>(entity))
                {
                    GetComponent<TransformComponent>(entity)->SetDirty(true);
                }
            }
            return result;
        }

        template <typename Component>
        Component* GetComponent(Entity entity)
        {
            return component_manager.GetComponent<Component>(entity);
        }

        void* GetComponent(Entity entity, won::TypeId type_id)
        {
            return component_manager.GetComponent(entity, type_id);
        }

        const void* GetComponent(Entity entity, won::TypeId type_id) const
        {
            return component_manager.GetComponent(entity, type_id);
        }

        template <typename Component>
        void RemoveComponent(Entity entity)
        {
            component_manager.RemoveComponent<Component>(entity);

            if constexpr (std::is_same_v<Component, HierarchyComponent>)
            {
                SetHierarchyTopologyDirty(true);
                if (HasComponent<TransformComponent>(entity))
                {
                    GetComponent<TransformComponent>(entity)->SetDirty(true);
                }
            }
        }

        void RemoveComponent(Entity entity, won::TypeId type_id)
        {
            component_manager.RemoveComponent(entity, type_id);

            const won::TypeDesc* hierarchy_desc = reflection::TypeMeta<HierarchyComponent>::Get();
            if (hierarchy_desc && type_id == hierarchy_desc->type_id)
            {
                SetHierarchyTopologyDirty(true);
                if (HasComponent<TransformComponent>(entity))
                {
                    GetComponent<TransformComponent>(entity)->SetDirty(true);
                }
            }
        }

        template <typename Component>
        bool HasComponent(Entity entity) const
        {
            return component_manager.HasComponent<Component>(entity);
        }

        bool HasComponent(Entity entity, won::TypeId type_id) const
        {
            return component_manager.HasComponent(entity, type_id);
        }

        Vector<const won::TypeDesc*> GetComponentTypes() const
        {
            return component_manager.GetComponentTypes();
        }

        template <typename Component>
        std::shared_ptr<ComponentArray<Component>> GetComponentArray()
        {
            return component_manager.GetComponentArray<Component>();
        }

        std::shared_ptr<IComponentArray> GetComponentArray(won::TypeId type_id)
        {
            return component_manager.GetComponentArray(type_id);
        }

        std::shared_ptr<const IComponentArray> GetComponentArray(won::TypeId type_id) const
        {
            return component_manager.GetComponentArray(type_id);
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
                        if (system->GetExecutionPolicy() == SystemExecutionPolicy::Synchronous)
                        {
                            jobsystem::Wait(ctx);
                            system->Update(*this, delta_time);
                        }
                        else
                        {
                            jobsystem::Execute(ctx, [this, system, delta_time](jobsystem::JobArgs args) { system->Update(*this, delta_time); });
                        }
                    }
                }
                jobsystem::Wait(ctx);
            }

            if (cpu_bvh_dirty)
            {
                BuildBVH();
            }

        }

        const Vector<Entity>& GetEntities() const
        {
            return entities;
        }

        uint64 GetUpdateIndex() const
        {
            return update_index;
        }

        void SetUpdateIndex(uint64 value)
        {
            update_index = value;
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
                if (geometry.IsExcludeFromBVH() || !geometry.mesh || !geometry.mesh->IsValid() || !transform_array->HasData(entity))
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

            scene_bvh.Build(primitives, 1);
            cpu_bvh_dirty = false;
        }

        bool BuildGPUBVH()
        {
            const bool ddgi_trace_required = render_data.shader_environment_lighting.gi_mode == SHADER_ENVIRONMENT_GI_MODE_DDGI &&
                (render_data.shader_ddgi_volume.flags & SHADER_DDGI_FLAG_ACTIVE) != 0 &&
                render_data.shader_ddgi_volume.total_probe_count > 0;
            if (!ddgi_trace_required)
            {
                render_data.shader_bvh_nodes.clear();
                render_data.shader_bvh_instances.clear();
                gpu_bvh_dirty = true;
                return true;
            }

            auto geometry_array = GetComponentArray<GeometryComponent>().get();
            auto transform_array = GetComponentArray<TransformComponent>().get();
            auto material_array = GetComponentArray<MaterialComponent>().get();
            if (!geometry_array || !transform_array)
            {
                render_data.shader_bvh_nodes.clear();
                render_data.shader_bvh_instances.clear();
                gpu_bvh_dirty = false;
                return true;
            }

            profiler::ScopedRangeCPU range("Scene::BuildGPUBVH");

            bool mesh_gpu_bvh_pending = false;
            for (Size geometry_component_index = 0; geometry_component_index < geometry_array->GetSize(); ++geometry_component_index)
            {
                const GeometryComponent& geometry = geometry_array->data[geometry_component_index];
                if (geometry.IsExcludeFromBVH() || !geometry.mesh || !geometry.mesh->IsValid() || !geometry.mesh->gpu_bvh.dirty)
                {
                    continue;
                }

                mesh_gpu_bvh_pending = true;
                rendering::utils::EnqueueGPUBVHBuild(geometry.mesh);
            }
            if (!gpu_bvh_dirty && !mesh_gpu_bvh_pending)
            {
                return true;
            }

            render_data.shader_bvh_nodes.clear();
            render_data.shader_bvh_instances.clear();

            math::bvh::BVH bvh;
            Vector<math::bvh::BVHPrimitive> primitives;
            Vector<ShaderBVHInstance> source_instances;

            for (Size geometry_component_index = 0; geometry_component_index < geometry_array->GetSize(); ++geometry_component_index)
            {
                const Entity entity = geometry_array->index_to_entity[geometry_component_index];
                const GeometryComponent& geometry = geometry_array->data[geometry_component_index];
                if (geometry.IsExcludeFromBVH() || !geometry.mesh || !geometry.mesh->IsValid() || !transform_array->HasData(entity))
                {
                    continue;
                }

                const resource::Mesh::GPUBVH& gpu_bvh = geometry.mesh->gpu_bvh;
                if (gpu_bvh.dirty)
                {
                    mesh_gpu_bvh_pending = true;
                    if (!gpu_bvh.IsValid())
                    {
                        continue;
                    }
                }
                else if (!gpu_bvh.IsValid())
                {
                    continue;
                }

                const TransformComponent& transform = transform_array->GetData(entity);
                if (!transform.world_bounds.IsValid())
                {
                    continue;
                }

                const MaterialComponent* material = material_array && material_array->HasData(entity) ? &material_array->GetData(entity) : nullptr;

                ShaderBVHInstance shader_instance = {};
                const XMMATRIX world_transform = transform.GetWorldTransform();
                const XMMATRIX world_to_local = XMMatrixInverse(nullptr, world_transform);
                XMStoreFloat4x4(&shader_instance.local_to_world, world_transform);
                XMStoreFloat4x4(&shader_instance.world_to_local, world_to_local);
                shader_instance.bounds_min = transform.world_bounds.min;
                shader_instance.bounds_max = transform.world_bounds.max;
                shader_instance.blas_node_buffer = gpu_bvh.node_srv.descriptor_index;
                shader_instance.blas_primitive_buffer = gpu_bvh.primitive_srv.descriptor_index;
                shader_instance.blas_node_count = gpu_bvh.node_count;
                shader_instance.blas_primitive_count = gpu_bvh.primitive_count;
                shader_instance.geometry_offset = geometry.geometry_offset;
                shader_instance.material_offset = material ? material->material_offset : 0;
                shader_instance.material_count = material ? static_cast<uint32>(material->GetMaterialSlotCount()) : 0;

                primitives.push_back(math::bvh::MakePrimitive(transform.world_bounds, static_cast<uint32>(source_instances.size())));
                source_instances.push_back(shader_instance);
            }

            bvh.Build(primitives);
            if (!bvh.IsValid())
            {
                gpu_bvh_dirty = mesh_gpu_bvh_pending;
                return !mesh_gpu_bvh_pending;
            }

            render_data.shader_bvh_nodes.resize(bvh.nodes.size());
            for (Size i = 0; i < bvh.nodes.size(); ++i)
            {
                const math::bvh::BVHNode& node = bvh.nodes[i];
                ShaderBVHNode& shader_node = render_data.shader_bvh_nodes[i];
                shader_node.bounds_min = node.bounds.min;
                shader_node.bounds_max = node.bounds.max;
                shader_node.left_index = node.left_index;
                shader_node.right_index = node.right_index;
                shader_node.first_primitive = static_cast<uint32>(node.first_primitive);
                shader_node.primitive_count = static_cast<uint32>(node.primitive_count);
                shader_node.padding = { 0, 0 };
            }

            render_data.shader_bvh_instances.resize(bvh.primitive_indices.size());
            for (Size i = 0; i < bvh.primitive_indices.size(); ++i)
            {
                const uint32 source_index = bvh.primitives[bvh.primitive_indices[i]].user_data;
                if (source_index < source_instances.size())
                {
                    render_data.shader_bvh_instances[i] = source_instances[source_index];
                }
            }
            gpu_bvh_dirty = mesh_gpu_bvh_pending;
            return !mesh_gpu_bvh_pending;
        }

        bool RayCastBVH(const math::Ray& ray, RayCastBVHHit& out_hit, bool use_local_bvh = true)
        {
            out_hit = {};

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
                        if (!mesh.cpu_bvh.IsValid())
                        {
                            mesh.BuildBVH();
                        }

                        if (mesh.cpu_bvh.IsValid())
                        {
                            used_local_bvh = true;
                            math::bvh::BVHRayHit local_bvh_hit = {};
                            math::bvh::IntersectClosest(mesh.cpu_bvh, local_ray, 0.0f, (std::numeric_limits<float>::max)(),
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
                    out_hit.hit.entity = entity;
                    out_hit.triangle_index = closest_triangle_index;
                    out_hit.barycentric = closest_barycentric;
                    return true;
                }, bvh_hit);

            if (!hit)
            {
                out_hit = {};
                return false;
            }

            out_hit.hit.distance = bvh_hit.distance;
            return true;
        }

        bool RayCastCollider3D(const math::Ray& ray, RayCastHit& out_hit, float max_distance = (std::numeric_limits<float>::max)())
        {
            out_hit = {};

            auto collider_3d_array = GetComponentArray<Collider3DComponent>().get();
            if (!collider_3d_array)
            {
                return false;
            }

            const XMVECTOR ray_direction = XMLoadFloat3(&ray.direction);
            const float ray_direction_length_sq = XMVectorGetX(XMVector3LengthSq(ray_direction));
            if (ray_direction_length_sq <= 0.000001f || max_distance < 0.0f)
            {
                return false;
            }

            math::Ray query_ray = ray;
            XMStoreFloat3(&query_ray.direction, XMVector3Normalize(ray_direction));

            bool found_hit = false;
            float closest_distance = max_distance;
            for (Size i = 0; i < collider_3d_array->GetSize(); ++i)
            {
                const Collider3DComponent& collider = collider_3d_array->data[i];
                if (!collider.IsEnabled() || !collider.world_bounds.IsValid())
                {
                    continue;
                }

                float distance = 0.0f;
                bool hit = false;
                if (collider.shape_type == Collider3DComponent::Sphere)
                {
                    const XMVECTOR origin = XMLoadFloat3(&query_ray.origin);
                    const XMVECTOR direction = XMLoadFloat3(&query_ray.direction);
                    const XMVECTOR center = XMLoadFloat3(&collider.world_sphere.center);
                    const XMVECTOR m = origin - center;
                    const float b = XMVectorGetX(XMVector3Dot(m, direction));
                    const float c = XMVectorGetX(XMVector3Dot(m, m)) - collider.world_sphere.radius * collider.world_sphere.radius;
                    const float discriminant = b * b - c;
                    if (!(c > 0.0f && b > 0.0f) && discriminant >= 0.0f)
                    {
                        distance = -b - std::sqrt(discriminant);
                        if (distance < 0.0f)
                        {
                            distance = 0.0f;
                        }
                        hit = distance <= closest_distance;
                    }
                }
                else
                {
                    hit = collider.world_bounds.IntersectAABB(query_ray, 0.0f, closest_distance, distance);
                }

                if (!hit)
                {
                    continue;
                }

                found_hit = true;
                closest_distance = distance;
                out_hit.entity = collider_3d_array->index_to_entity[i];
                out_hit.distance = distance;
            }

            if (!found_hit)
            {
                out_hit = {};
                return false;
            }
            return true;
        }

        void OverlapCollider3D(const math::AABB& bounds, Vector<OverlapHit>& out_hits)
        {
            out_hits.clear();
            if (!bounds.IsValid())
            {
                return;
            }

            auto collider_3d_array = GetComponentArray<Collider3DComponent>().get();
            if (!collider_3d_array)
            {
                return;
            }

            for (Size i = 0; i < collider_3d_array->GetSize(); ++i)
            {
                const Collider3DComponent& collider = collider_3d_array->data[i];
                const math::AABB& collider_bounds = collider.world_bounds;
                if (!collider.IsEnabled() || !collider_bounds.IsValid())
                {
                    continue;
                }

                const bool overlap =
                    bounds.min.x <= collider_bounds.max.x && bounds.max.x >= collider_bounds.min.x &&
                    bounds.min.y <= collider_bounds.max.y && bounds.max.y >= collider_bounds.min.y &&
                    bounds.min.z <= collider_bounds.max.z && bounds.max.z >= collider_bounds.min.z;
                if (overlap)
                {
                    out_hits.push_back({ collider_3d_array->index_to_entity[i] });
                }
            }
        }

        void OverlapCollider3D(const math::Sphere& sphere, Vector<OverlapHit>& out_hits)
        {
            out_hits.clear();
            auto collider_3d_array = GetComponentArray<Collider3DComponent>().get();
            if (!collider_3d_array)
            {
                return;
            }

            const float sphere_radius = (std::max)(0.0f, sphere.radius);
            for (Size i = 0; i < collider_3d_array->GetSize(); ++i)
            {
                const Collider3DComponent& collider = collider_3d_array->data[i];
                if (!collider.IsEnabled() || !collider.world_bounds.IsValid())
                {
                    continue;
                }

                bool overlap = false;
                if (collider.shape_type == Collider3DComponent::Sphere)
                {
                    const float dx = sphere.center.x - collider.world_sphere.center.x;
                    const float dy = sphere.center.y - collider.world_sphere.center.y;
                    const float dz = sphere.center.z - collider.world_sphere.center.z;
                    const float radius_sum = sphere_radius + collider.world_sphere.radius;
                    overlap = dx * dx + dy * dy + dz * dz <= radius_sum * radius_sum;
                }
                else
                {
                    const math::AABB& bounds = collider.world_bounds;
                    const float closest_x = (std::max)(bounds.min.x, (std::min)(sphere.center.x, bounds.max.x));
                    const float closest_y = (std::max)(bounds.min.y, (std::min)(sphere.center.y, bounds.max.y));
                    const float closest_z = (std::max)(bounds.min.z, (std::min)(sphere.center.z, bounds.max.z));
                    const float dx = sphere.center.x - closest_x;
                    const float dy = sphere.center.y - closest_y;
                    const float dz = sphere.center.z - closest_z;
                    overlap = dx * dx + dy * dy + dz * dz <= sphere_radius * sphere_radius;
                }

                if (overlap)
                {
                    out_hits.push_back({ collider_3d_array->index_to_entity[i] });
                }
            }
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
                    DoubleSided = 1 << 2,
                };

                ObjectPushConstants push_constants;
                std::shared_ptr<rendering::RHIResource> index_buffer;
                uint32 index_offset = 0;
                uint32 index_count = 0;
                uint32 flags = None;
                uint32 shader_type = SHADER_MATERIAL_TYPE_PBR;
                resource::PrimitiveTopology primitive_topology = resource::PrimitiveTopology::TriangleList;

                bool IsTransparent() const
                {
                    return (flags & Transparent) != 0;
                }

                bool IsCastShadow() const
                {
                    return (flags & CastShadow) != 0;
                }

                bool IsDoubleSided() const
                {
                    return (flags & DoubleSided) != 0;
                }
            };

            struct RenderShadowSlice
            {
                uint32 light_index = 0;
                float4x4 view_projection = math::IDENTITY_MATRIX;
                int4 shadow_map_atlas_rect = { -1, -1, 0, 0 };

                bool HasShadowMapAtlasRect() const { return shadow_map_atlas_rect.z > 0 && shadow_map_atlas_rect.w > 0; }
            };

            struct Sprite3DRenderable
            {
                enum Flags : uint32
                {
                    None = 0,
                    Billboard = 1 << 0,
                    Transparent = 1 << 1,
                };

                uint32 instance_index = 0;
                uint32 material_index = 0;
                float2 size = { 1.0f, 1.0f };
                float2 pivot = { 0.5f, 0.5f };
                float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
                uint32 flags = None;

                bool IsBillboard() const
                {
                    return (flags & Billboard) != 0;
                }

                bool IsTransparent() const
                {
                    return (flags & Transparent) != 0;
                }
            };

            struct Sprite2DRenderable
            {
                uint32 material_index = 0;
                float2 anchor = { 0.0f, 0.0f };
                float2 position = { 0.0f, 0.0f };
                float2 size = { 1.0f, 1.0f };
                float2 pivot = { 0.5f, 0.5f };
                float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
                int32 layer = 0;
            };

            struct Text3DRenderable
            {
                enum Flags : uint32
                {
                    None = 0,
                    Billboard = 1 << 0,
                };

                uint32 instance_index = 0;
                uint32 material_index = 0;
                std::shared_ptr<resource::Font> font;
                float2 position = { 0.0f, 0.0f };
                float2 size = { 0.0f, 0.0f };
                float4 uv_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
                uint32 flags = None;

                bool IsBillboard() const
                {
                    return (flags & Billboard) != 0;
                }
            };

            struct Text2DRenderable
            {
                uint32 material_index = 0;
                std::shared_ptr<resource::Font> font;
                float2 anchor = { 0.0f, 0.0f };
                float2 position = { 0.0f, 0.0f };
                float2 size = { 0.0f, 0.0f };
                float4 uv_rect = { 0.0f, 0.0f, 0.0f, 0.0f };
                int32 layer = 0;
            };

            ShaderSky shader_sky;
            ShaderEnvironmentLighting shader_environment_lighting;
            ShaderDDGIVolume shader_ddgi_volume;

            Vector<ShaderInstance> shader_instances;
            Vector<ShaderGeometry> shader_geometries;
            Vector<ShaderMaterial> shader_materials;
            Vector<float4> shader_bone_matrices;
            Vector<ShaderBVHNode> shader_bvh_nodes;
            Vector<ShaderBVHInstance> shader_bvh_instances;
            Vector<Renderable> mesh_renderables;
            Vector<Renderable> double_sided_renderables;
            Vector<Renderable> line_renderables;
            Vector<Renderable> point_renderables;
            Vector<Sprite2DRenderable> sprite_2d_renderables;
            Vector<Text2DRenderable> text_2d_renderables;
            Vector<Sprite3DRenderable> sprite_3d_renderables;
            Vector<Text3DRenderable> text_3d_renderables;

            Vector<ShaderLight> shader_lights; // all lights
            Vector<ShaderShadowCascade> shader_shadow_cascades; // lights with shadow map
            Vector<RenderShadowSlice> render_shadow_slices;
            uint4 forward_light_mask;
            uint2 shadow_map_atlas_size = { 0, 0 };
            math::AABB shadow_caster_world_bound;

            Entity ddgi_volume_entity = INVALID_ENTITY;

            void Clear()
            {
                shader_instances.clear();
                shader_geometries.clear();
                shader_materials.clear();
                shader_bone_matrices.clear();
                shader_bvh_nodes.clear();
                shader_bvh_instances.clear();
                mesh_renderables.clear();
                double_sided_renderables.clear();
                line_renderables.clear();
                point_renderables.clear();
                sprite_2d_renderables.clear();
                text_2d_renderables.clear();
                sprite_3d_renderables.clear();
                text_3d_renderables.clear();

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

        const Vector<physics::Collider3DTriggerEvent>& GetCollider3DTriggerEvents() const
        {
            return physics_world->GetTriggerEvents();
        }

        won::physics::PhysicsWorld* GetPhysicsWorld() const
        {
            return physics_world.get();
        }

        const math::bvh::BVH& GetSceneBVH() const
        {
            return scene_bvh;
        }

        void SetHierarchyTopologyDirty(bool value = true)
        {
            hierarchy_topology_dirty = value;
        }

        bool IsHierarchyTopologyDirty() const
        {
            return hierarchy_topology_dirty;
        }

    private:
        bool hierarchy_topology_dirty = true;

        RenderData render_data;
        ComponentManager component_manager;
        Vector<Entity> entities;
        Entity next_entity = INVALID_ENTITY + 1;
        math::bvh::BVH scene_bvh;
        Vector<Entity> scene_bvh_entities;
        Vector<std::shared_ptr<System>> systems;
        Vector<Vector<uint32>> system_execution_batches;
        uint64 update_index = 0;
        bool cpu_bvh_dirty = true;
        bool gpu_bvh_dirty = true;
        bool system_schedule_dirty = true;
        std::unique_ptr<won::physics::PhysicsWorld> physics_world;
    };
}
