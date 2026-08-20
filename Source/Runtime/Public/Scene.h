#pragma once
#include "ComponentManager.h"
#include "Entity.h"
#include "System.h"
#include "SceneComponents.h"
#include "SceneRequest.h"
#include "BuiltinTypeMeta.h"
#include "Systems.h"
#include "PhysicsWorld.h"
#include "NavMesh.h"
#include "AudioMixer.h"
#include "BVH.h"

#include "Types.h"
#include "MathUtils.h"
#include "Profiler.h"

#include <algorithm>
#include <atomic>
#include <memory>
#include <utility>

namespace won::rendering
{
    struct GPUScene;
}

namespace won::ecs
{
    struct SceneDesc
    {
        script::ScriptRuntime* script_runtime = nullptr; // Non-owning.
        won::physics::PhysicsWorldDesc physics;
        won::audio::AudioMixer* audio_mixer = nullptr; // Non-owning. Owned by Application.
        bool enable_simulation = true;
    };

    struct RayCastHit
    {
        Entity entity = INVALID_ENTITY;
        float distance = (std::numeric_limits<float>::max)();
        float3 point = { 0.0f, 0.0f, 0.0f };
        float3 normal = { 0.0f, 0.0f, 0.0f };
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
        Scene(const SceneDesc& desc = {});
        ~Scene();

        Entity CreateEntity();

        Entity ReviveEntity(Entity id);

        bool IsEntityAlive(Entity entity) const;

        void DestroyEntity(Entity entity);

        void ClearEntities();

        void SwapContents(Scene& other);

        template <typename Component, typename... Args>
        Component* AddComponent(Entity entity, Args&&... args)
        {
            Component component { std::forward<Args>(args)... };
            Component* result = component_manager.AddComponent<Component>(entity, component);
            if constexpr (ComponentMaskFromType<Component>() != none_component_mask)
            {
                if (result)
                {
                    MarkGpuDirty(ComponentMaskFromType<Component>());
                }
            }

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

        void* AddComponent(Entity entity, won::TypeId type_id, const void* component);

        void* AddComponent(Entity entity, const won::TypeDesc* type_desc);

		// we assume that if you are getting a component you are going to modify it
        // !! If you don't want to modify it, then use the const version of GetComponent
        template <typename Component>
        Component* GetComponent(Entity entity)
        {
            Component* result = component_manager.GetComponent<Component>(entity);
            if constexpr (ComponentMaskFromType<Component>() != none_component_mask)
            {
                if (result)
                {
                    MarkGpuDirty(ComponentMaskFromType<Component>());
                }
            }
            return result;
        }

        void* GetComponent(Entity entity, won::TypeId type_id);

        const void* GetComponent(Entity entity, won::TypeId type_id) const
        {
            return component_manager.GetComponent(entity, type_id);
        }

        template <typename Component>
        void RemoveComponent(Entity entity)
        {
            if constexpr (std::is_same_v<Component, Collider3DComponent>)
            {
                if (physics_world)
                    physics_world->RemoveBody(entity);
            }

            if constexpr (ComponentMaskFromType<Component>() != none_component_mask)
            {
                if (HasComponent<Component>(entity))
                {
                    MarkGpuDirty(ComponentMaskFromType<Component>());
                }
            }
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

        void RemoveComponent(Entity entity, won::TypeId type_id);

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

        template <typename Component>
        std::shared_ptr<const ComponentArray<Component>> GetComponentArray() const
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

        void AddSystem(std::unique_ptr<System> system);

        void BuildSystemSchedule();

        const Vector<std::unique_ptr<System>>& GetSystems() const { return systems; }
        const Vector<Vector<uint32>>& GetSystemExecutionBatches() const { return system_execution_batches; }

        void Update(float delta_time);

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

        void MarkGpuDirty(ComponentMask mask)
        {
            gpu_dirty_mask.fetch_or(mask, std::memory_order_relaxed);
        }

        ComponentMask GetGpuDirtySnapshot() const
        {
            return gpu_dirty_snapshot;
        }

        void BuildBVH();

        bool BuildGPUBVH();

        bool RayCastBVH(const math::Ray& ray, RayCastBVHHit& out_hit, bool use_local_bvh = true, uint32 layer_mask = 0xFFFFFFFF);

        bool RayCastCollider3D(const math::Ray& ray, RayCastHit& out_hit, float max_distance = (std::numeric_limits<float>::max)(), uint32 layer_mask = 0xFFFFFFFF);

        void OverlapCollider3D(const math::Sphere& sphere, Vector<OverlapHit>& out_hits);

        rendering::GPUScene& GetGPUScene();

        const Vector<physics::Collider3DTriggerEvent>& GetCollider3DTriggerEvents() const
        {
            return physics_world->GetTriggerEvents();
        }

        won::physics::PhysicsWorld* GetPhysicsWorld() const
        {
            return physics_world.get();
        }

        won::nav::NavMesh* GetNavMesh() const
        {
            return nav_mesh.get();
        }

        void SetNavMesh(std::unique_ptr<won::nav::NavMesh> value)
        {
            nav_mesh = std::move(value);
        }

        void QueueUIClick(Entity entity)
        {
            ui_click_queue.push_back(entity);
        }

        const Vector<Entity>& GetUIClickEvents() const
        {
            return ui_click_queue;
        }

        void ClearUIClickEvents()
        {
            ui_click_queue.clear();
        }

        void QueueAnimationEvent(Entity entity, const String& name)
        {
            animation_event_queue.push_back({ entity, name });
        }

        const Vector<std::pair<Entity, String>>& GetAnimationEvents() const
        {
            return animation_event_queue;
        }

        void ClearAnimationEvents()
        {
            animation_event_queue.clear();
        }

        void QueueSequenceEvent(Entity entity, const String& name)
        {
            sequence_event_queue.push_back({ entity, name });
        }

        const Vector<std::pair<Entity, String>>& GetSequenceEvents() const
        {
            return sequence_event_queue;
        }

        void ClearSequenceEvents()
        {
            sequence_event_queue.clear();
        }

        struct WaterSimulationState
        {
            double step_accumulator = 0.0;
            uint64 step_count = 0;
            uint32 pending_steps = 0;
        };

        WaterSimulationState& GetWaterSimulation()
        {
            return water_simulation;
        }

        const WaterSimulationState& GetWaterSimulation() const
        {
            return water_simulation;
        }

        void QueueWaterRipple(const WaterRippleRequest& request)
        {
            water_ripple_queue.push_back(request);
        }

        const Vector<WaterRippleRequest>& GetWaterRippleQueue() const
        {
            return water_ripple_queue;
        }

        void ClearWaterRippleQueue()
        {
            water_ripple_queue.clear();
        }

        void QueuePrefabSpawn(const PrefabSpawnRequest& request)
        {
            prefab_spawn_queue.push_back(request);
        }

        const Vector<PrefabSpawnRequest>& GetPrefabSpawnQueue() const
        {
            return prefab_spawn_queue;
        }

        void ClearPrefabSpawnQueue()
        {
            prefab_spawn_queue.clear();
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

        ComponentManager component_manager;
        Vector<Entity> entities;
        Entity next_entity = INVALID_ENTITY + 1;
        math::bvh::BVH scene_bvh;
        Vector<Entity> scene_bvh_entities;
        Vector<std::unique_ptr<System>> systems;
        Vector<Vector<uint32>> system_execution_batches;
        uint64 update_index = 0;
        std::atomic<ComponentMask> gpu_dirty_mask = 0;
        ComponentMask gpu_dirty_snapshot = ~0ull;
        bool cpu_bvh_dirty = true;
        bool gpu_bvh_dirty = true;
        bool system_schedule_dirty = true;
        std::unique_ptr<won::physics::PhysicsWorld> physics_world;
        std::unique_ptr<won::nav::NavMesh> nav_mesh;
        std::unique_ptr<rendering::GPUScene> gpu_scene;
        Vector<Entity> ui_click_queue;
        Vector<std::pair<Entity, String>> animation_event_queue;
        Vector<std::pair<Entity, String>> sequence_event_queue;
        Vector<PrefabSpawnRequest> prefab_spawn_queue;
        Vector<WaterRippleRequest> water_ripple_queue;
        WaterSimulationState water_simulation = {};
    };
}
