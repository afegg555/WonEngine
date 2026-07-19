#pragma once
#include "ComponentManager.h"
#include "Entity.h"
#include "System.h"
#include "SceneComponents.h"
#include "PrefabSpawnRequest.h"
#include "BuiltinTypeMeta.h"
#include "Systems.h"
#include "PhysicsWorld.h"
#include "AudioMixer.h"
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
        won::audio::AudioMixer* audio_mixer = nullptr; // Non-owning. Owned by Application.
        bool enable_simulation = true;
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
        Scene(const SceneDesc& desc = {});

        Entity CreateEntity();

        void DestroyEntity(Entity entity);

        void ClearEntities();

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

        void* AddComponent(Entity entity, won::TypeId type_id, const void* component);

        void* AddComponent(Entity entity, const won::TypeDesc* type_desc);

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
            if constexpr (std::is_same_v<Component, Collider3DComponent>)
            {
                if (physics_world)
                    physics_world->RemoveBody(entity);
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

        std::shared_ptr<IComponentArray> GetComponentArray(won::TypeId type_id)
        {
            return component_manager.GetComponentArray(type_id);
        }

        std::shared_ptr<const IComponentArray> GetComponentArray(won::TypeId type_id) const
        {
            return component_manager.GetComponentArray(type_id);
        }

        void AddSystem(const std::shared_ptr<System>& system);

        void BuildSystemSchedule();

        const Vector<std::shared_ptr<System>>& GetSystems() const { return systems; }
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

        void BuildBVH();

        bool BuildGPUBVH();

        bool RayCastBVH(const math::Ray& ray, RayCastBVHHit& out_hit, bool use_local_bvh = true, uint32 layer_mask = 0xFFFFFFFF);

        bool RayCastCollider3D(const math::Ray& ray, RayCastHit& out_hit, float max_distance = (std::numeric_limits<float>::max)(), uint32 layer_mask = 0xFFFFFFFF);

        void OverlapCollider3D(const math::AABB& bounds, Vector<OverlapHit>& out_hits);

        void OverlapCollider3D(const math::Sphere& sphere, Vector<OverlapHit>& out_hits);

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
                float3 world_position = {};
                math::AABB aabb = {};
                uint32 index_offset = 0;
                uint32 index_count = 0;
                uint32 flags = None;
                uint32 shader_type = SHADER_MATERIAL_TYPE_PBR;
                resource::MaterialBlendMode blend_mode = resource::MaterialBlendMode::Opaque;
                uint32 layer_mask = 0xFFFFFFFF;
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
                    None        = 0,
                    Text        = 1 << 0,
                    Billboard   = 1 << 1,
                    Transparent = 1 << 2,
                    Particle    = 1 << 3,
                };

                uint32 instance_index = 0;
                uint32 material_index = 0;
                float3 world_position = {};
                math::AABB aabb = {};
                float2 size = { 1.0f, 1.0f };
                float2 pivot = { 0.5f, 0.5f };
                float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
                uint32 flags = None;
                resource::MaterialBlendMode blend_mode = resource::MaterialBlendMode::Alpha;
                uint32 layer_mask = 0xFFFFFFFF;
                // For particles: bindless descriptor of the per-frame float4 buffer holding
                // interleaved [position, color] pairs, indexed by instance_index.
                uint32 resource_index = 0;
                std::shared_ptr<resource::Font> font;

                bool IsText()        const { return (flags & Text) != 0; }
                bool IsBillboard()   const { return (flags & Billboard) != 0; }
                bool IsTransparent() const { return (flags & Transparent) != 0; }
                bool IsParticle()    const { return (flags & Particle) != 0; }
            };

            struct Sprite2DRenderable
            {
                enum Flags : uint32
                {
                    None = 0,
                    Text = 1 << 0,
                };

                uint32 material_index = 0;
                float2 anchor = { 0.0f, 0.0f };
                float2 position = { 0.0f, 0.0f };
                float2 size = { 1.0f, 1.0f };
                float2 pivot = { 0.5f, 0.5f };
                float2 reference_resolution = { 0.0f, 0.0f };
                float4 uv_rect = { 0.0f, 0.0f, 1.0f, 1.0f };
                int32  layer = 0;
                uint32 layer_mask = 0xFFFFFFFF;
                float  match = 0.5f;
                uint32 flags = None;
                std::shared_ptr<resource::Font> font;

                bool IsText() const { return (flags & Text) != 0; }
            };

            ShaderEnvironment shader_environment;
            ShaderDDGIVolume shader_ddgi_volume;
            ShaderReflectionProbe shader_reflection_probe;

            Vector<ShaderInstance> shader_instances;
            Vector<ShaderGeometry> shader_geometries;
            Vector<ShaderMaterial> shader_materials;
            Vector<float4> shader_bone_matrices;
            Vector<ShaderBVHNode> shader_bvh_nodes;
            Vector<ShaderBVHInstance> shader_bvh_instances;
            Vector<Renderable> opaque_renderables;
            Vector<Renderable> transparent_renderables;
            Vector<Renderable> line_renderables;
            Vector<Renderable> point_renderables;
            Vector<Sprite2DRenderable> sprite_2d_renderables;
            Vector<Sprite3DRenderable> sprite_3d_renderables;
            // Per-frame CPU particle GPU data: interleaved [position, color] float4 pairs,
            // uploaded to a bindless buffer and indexed by particle Sprite3DRenderable.instance_index.
            Vector<float4> particle_instances;
            Vector<ShaderDecal> shader_decals;

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
                shader_decals.clear();
                shader_geometries.clear();
                shader_materials.clear();
                shader_bone_matrices.clear();
                shader_bvh_nodes.clear();
                shader_bvh_instances.clear();
                opaque_renderables.clear();
                transparent_renderables.clear();
                line_renderables.clear();
                point_renderables.clear();
                sprite_2d_renderables.clear();
                sprite_3d_renderables.clear();
                particle_instances.clear();

                shader_lights.clear();
                shader_shadow_cascades.clear();
                render_shadow_slices.clear();
                forward_light_mask = { 0,0,0,0 };
                shadow_map_atlas_size = { 0, 0 };
                shadow_caster_world_bound.Invalidate();
                shader_environment.Init();
                shader_ddgi_volume.Init();
                ddgi_volume_entity = INVALID_ENTITY;
            }
        };

        RenderData& GetRenderData()
        {
            return render_data;
        }

        const RenderData& GetRenderData() const
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
        Vector<Entity> ui_click_queue;
        Vector<std::pair<Entity, String>> animation_event_queue;
        Vector<PrefabSpawnRequest> prefab_spawn_queue;
    };
}
