#include "Scene.h"

#include "Backlog.h"

#include <typeinfo>

namespace won::ecs
{
    Scene::Scene(const SceneDesc& desc)
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
        component_manager.RegisterComponent<Canvas2DComponent>();
        component_manager.RegisterComponent<RectTransform2DComponent>();
        component_manager.RegisterComponent<ButtonComponent>();
        component_manager.RegisterComponent<LayoutComponent>();
        component_manager.RegisterComponent<CameraComponent>();
        component_manager.RegisterComponent<LightComponent>();
        component_manager.RegisterComponent<EnvironmentComponent>();
        component_manager.RegisterComponent<FogVolumeComponent>();
        component_manager.RegisterComponent<DDGIVolumeComponent>();
        component_manager.RegisterComponent<ReflectionProbeComponent>();
        component_manager.RegisterComponent<AnimationComponent>();
        component_manager.RegisterComponent<AnimationStateMachineComponent>();
        component_manager.RegisterComponent<ScriptComponent>();
        component_manager.RegisterComponent<Collider3DComponent>();
        component_manager.RegisterComponent<Rigidbody3DComponent>();
        component_manager.RegisterComponent<JointComponent>();
        component_manager.RegisterComponent<AudioSourceComponent>();
        component_manager.RegisterComponent<AudioListenerComponent>();
        component_manager.RegisterComponent<VisibilityLayerComponent>();
        component_manager.RegisterComponent<CollisionLayerComponent>();
        component_manager.RegisterComponent<TerrainComponent>();
        component_manager.RegisterComponent<ParticleEmitter3DComponent>();
        component_manager.RegisterComponent<DecalComponent>();

        if (desc.script_runtime && desc.enable_simulation)
        {
            AddSystem(std::make_shared<ScriptUpdateSystem>(desc.script_runtime));
        }
        AddSystem(std::make_shared<TransformUpdateSystem>());
        if (desc.enable_simulation)
        {
            AddSystem(std::make_shared<PhysicsUpdateSystem>());
        }
        if (desc.script_runtime && desc.enable_simulation)
        {
            AddSystem(std::make_shared<ScriptEventDispatchSystem>(desc.script_runtime));
        }
        AddSystem(std::make_shared<EnvironmentUpdateSystem>());
        AddSystem(std::make_shared<CameraUpdateSystem>());
        AddSystem(std::make_shared<LightUpdateSystem>());
        AddSystem(std::make_shared<GeometryUpdateSystem>());
        AddSystem(std::make_shared<MaterialUpdateSystem>());
        if (desc.enable_simulation)
        {
            AddSystem(std::make_shared<AnimationStateMachineSystem>());
        }
        AddSystem(std::make_shared<AnimationUpdateSystem>(desc.enable_simulation));
        AddSystem(std::make_shared<RenderableUpdateSystem>());
        AddSystem(std::make_shared<SpriteUpdateSystem>());
        AddSystem(std::make_shared<TextUpdateSystem>());
        if (desc.enable_simulation)
        {
            AddSystem(std::make_shared<ParticleUpdateSystem>());
        }
        AddSystem(std::make_shared<DecalUpdateSystem>());
        if (desc.enable_simulation)
        {
            AddSystem(std::make_shared<AudioUpdateSystem>(desc.audio_mixer));
        }

        physics_world = std::make_unique<won::physics::PhysicsWorld>(desc.physics);
    }

    void Scene::AddSystem(const std::shared_ptr<System>& system)
    {
        if (system)
        {
            systems.push_back(system);
            system_schedule_dirty = true;
        }
    }

    void Scene::BuildSystemSchedule()
    {
        if (system_schedule_dirty)
        {
            system_execution_batches.clear();

            const uint32 system_count = static_cast<uint32>(systems.size());
            Vector<ComponentMask> readonly_masks(system_count);
            Vector<ComponentMask> write_masks(system_count);
            for (uint32 i = 0; i < system_count; ++i)
            {
                if (systems[i])
                {
                    readonly_masks[i] = systems[i]->GetReadOnlyMask();
                    write_masks[i] = systems[i]->GetWriteMask();
                }
            }

            for (uint32 phase = 0; phase < static_cast<uint32>(SystemPhase::Count); ++phase)
            {
                Vector<uint32> phase_systems;
                for (uint32 i = 0; i < system_count; ++i)
                {
                    if (systems[i] && static_cast<uint32>(systems[i]->GetPhase()) == phase)
                    {
                        phase_systems.push_back(i);
                    }
                }
                if (phase_systems.empty())
                {
                    continue;
                }

                const uint32 n = static_cast<uint32>(phase_systems.size());
                Vector<Vector<uint32>> graph(n);
                Vector<uint32> indegree(n, 0);

                auto add_edge = [&](uint32 from, uint32 to)
                    {
                        if (from == to) return;
                        graph[from].push_back(to);
                        ++indegree[to];
                    };

                for (uint32 a = 0; a < n; ++a)
                {
                    for (uint32 b = a + 1; b < n; ++b)
                    {
                        const uint32 i = phase_systems[a];
                        const uint32 j = phase_systems[b];
                        const bool raw_i_to_j = (write_masks[i] & readonly_masks[j]) != 0;
                        const bool raw_j_to_i = (write_masks[j] & readonly_masks[i]) != 0;
                        const bool waw = (write_masks[i] & write_masks[j]) != 0;
                        if (raw_i_to_j) add_edge(a, b);
                        if (raw_j_to_i) add_edge(b, a);
                        if (waw) add_edge(a, b); // deterministic for a < b
                    }
                }

                Vector<uint8> processed(n, 0);
                uint32 processed_count = 0;
                bool had_cycle = false;

                auto build_ready = [&]()
                    {
                        Vector<uint32> ready;
                        for (uint32 a = 0; a < n; ++a)
                        {
                            if (!processed[a] && indegree[a] == 0)
                            {
                                ready.push_back(a);
                            }
                        }
                        return ready;
                    };

                while (processed_count < n)
                {
                    Vector<uint32> batch_local = build_ready();
                    if (batch_local.empty())
                    {
                        if (!had_cycle)
                        {
                            String cycle_members;
                            for (uint32 a = 0; a < n; ++a)
                            {
                                if (!processed[a])
                                {
                                    if (!cycle_members.empty()) { cycle_members += ", "; }
                                    cycle_members += typeid(*systems[phase_systems[a]]).name();
                                }
                            }
                            backlog::Post("[Scheduler] system dependency cycle in phase " + std::to_string(phase) + ": " + cycle_members, backlog::LogLevel::Error);
                        }
                        had_cycle = true;
                        for (uint32 a = 0; a < n; ++a)
                        {
                            if (!processed[a]) { indegree[a] = 0; batch_local.push_back(a); break; }
                        }
                        if (batch_local.empty()) break;
                    }

                    Vector<uint32> batch_global;
                    batch_global.reserve(batch_local.size());
                    for (uint32 a : batch_local)
                    {
                        batch_global.push_back(phase_systems[a]);
                    }
                    system_execution_batches.push_back(std::move(batch_global));

                    for (uint32 a : batch_local)
                    {
                        processed[a] = 1;
                        ++processed_count;
                        for (uint32 to : graph[a])
                        {
                            if (indegree[to] > 0) --indegree[to];
                        }
                    }
                }
            }
            system_schedule_dirty = false;
        }
    }

    void Scene::Update(float delta_time)
    {
        BuildSystemSchedule();

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

    Entity Scene::CreateEntity()
    {
        Entity entity = next_entity++;
        entities.push_back(entity);
        return entity;
    }

    void Scene::DestroyEntity(Entity entity)
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
            if (physics_world && HasComponent<Collider3DComponent>(current))
                physics_world->RemoveBody(current);
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

    void Scene::ClearEntities()
    {
        component_manager.Clear();
        entities.clear();
        render_data.Clear();
        scene_bvh.Clear();
        scene_bvh_entities.clear();
        physics_world->Clear();
        prefab_spawn_queue.clear();
        animation_event_queue.clear();
        next_entity = INVALID_ENTITY + 1;
        SetBVHDirty();
    }

    void Scene::SwapContents(Scene& other)
    {
        std::swap(render_data, other.render_data);
        std::swap(component_manager, other.component_manager);
        std::swap(entities, other.entities);
        std::swap(next_entity, other.next_entity);
        std::swap(scene_bvh, other.scene_bvh);
        std::swap(scene_bvh_entities, other.scene_bvh_entities);
        std::swap(ui_click_queue, other.ui_click_queue);
        std::swap(animation_event_queue, other.animation_event_queue);
        std::swap(prefab_spawn_queue, other.prefab_spawn_queue);

        physics_world->Clear();

        SetBVHDirty();
        SetHierarchyTopologyDirty(true);
        system_schedule_dirty = true;
    }

    void Scene::BuildBVH()
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

    void* Scene::AddComponent(Entity entity, won::TypeId type_id, const void* component)
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

    void* Scene::AddComponent(Entity entity, const won::TypeDesc* type_desc)
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

    void Scene::RemoveComponent(Entity entity, won::TypeId type_id)
    {
        const won::TypeDesc* collider_desc = reflection::TypeMeta<Collider3DComponent>::Get();
        if (collider_desc && type_id == collider_desc->type_id && physics_world)
            physics_world->RemoveBody(entity);

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

    bool Scene::BuildGPUBVH()
    {
        const bool ddgi_trace_required = render_data.shader_environment.diffuse_gi_mode == SHADER_DIFFUSE_GI_MODE_DDGI &&
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
            shader_instance.geometry_offset = geometry.mesh ? geometry.mesh->geometry_offset : 0;
            shader_instance.material_offset = material && material->material ? material->material->material_offset : 0;
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

    bool Scene::RayCastBVH(const math::Ray& ray, RayCastBVHHit& out_hit, bool use_local_bvh, uint32 layer_mask)
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
                const VisibilityLayerComponent* layer = GetComponent<VisibilityLayerComponent>(entity);
                if ((layer_mask & (layer ? layer->layer_mask : 0xFFFFFFFF)) == 0)
                {
                    return false;
                }
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
                float3 closest_normal = {};
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

                    const XMVECTOR local_normal = XMVector3Normalize(XMVector3Cross(v1 - v0, v2 - v0));
                    const XMMATRIX normal_matrix = XMMatrixTranspose(inv_world_transform);

                    found_hit = true;
                    closest_distance = world_distance;
                    closest_barycentric = barycentric;
                    closest_triangle_index = triangle_index;
                    XMStoreFloat3(&closest_normal, XMVector3Normalize(XMVector3TransformNormal(local_normal, normal_matrix)));
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
                out_hit.hit.normal = closest_normal;
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
        const XMVECTOR hit_ray_origin = XMLoadFloat3(&ray.origin);
        const XMVECTOR hit_ray_direction = XMVector3Normalize(XMLoadFloat3(&ray.direction));
        XMStoreFloat3(&out_hit.hit.point, hit_ray_origin + hit_ray_direction * bvh_hit.distance);
        return true;
    }

    bool Scene::RayCastCollider3D(const math::Ray& ray, RayCastHit& out_hit, float max_distance, uint32 layer_mask)
    {
        out_hit = {};

        if (!physics_world)
        {
            return false;
        }

        physics::RayCastHit physics_hit;
        if (!physics_world->RayCast(ray.origin, ray.direction, max_distance, physics_hit, layer_mask))
        {
            return false;
        }

        out_hit.entity = physics_hit.entity;
        out_hit.distance = physics_hit.distance;
        out_hit.point = physics_hit.point;
        out_hit.normal = physics_hit.normal;
        return true;
    }

    void Scene::OverlapCollider3D(const math::Sphere& sphere, Vector<OverlapHit>& out_hits)
    {
        out_hits.clear();
        if (!physics_world)
        {
            return;
        }

        Vector<Entity> overlap_entities;
        physics_world->OverlapSphere(sphere.center, sphere.radius, overlap_entities);
        out_hits.reserve(overlap_entities.size());
        for (Entity entity : overlap_entities)
        {
            out_hits.push_back({ entity });
        }
    }
}
