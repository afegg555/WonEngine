#pragma once
#include "ComponentManager.h"
#include "Entity.h"
#include "System.h"
#include "SceneComponents.h"
#include "TransformUpdateSystem.h"
#include "CameraUpdateSystem.h"
#include "LightUpdateSystem.h"
#include "RenderDataUpdateSystem.h"
#include "ShaderInterop_Renderer.h"

#include "Types.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace won::rendering
{
    class RHIResource;
}

namespace won::ecs
{
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

            AddSystem(std::make_shared<TransformUpdateSystem>());
            AddSystem(std::make_shared<CameraUpdateSystem>());
            AddSystem(std::make_shared<LightUpdateSystem>());
            AddSystem(std::make_shared<RenderDataUpdateSystem>());
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
        }

        const Vector<Entity>& GetEntities() const
        {
            return entities;
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

            struct RenderShadowLight
            {
                //LightComponent::LightType type = LightComponent::Directional;
                uint32 light_index = 0;
                float4x4 view_projection;
                uint32 shadow_map_resolution = 0;
                int4 shadow_map_atlas_rect = { -1, -1, 0, 0 };

                bool HasShadowMapAtlasRect() const { return shadow_map_atlas_rect.z > 0 && shadow_map_atlas_rect.w > 0; }
            };

            Vector<ShaderInstance> shader_instances;
            Vector<ShaderGeometry> shader_geometries;
            Vector<ShaderMaterial> shader_materials;
            Vector<Renderable> renderables;

            Vector<ShaderLight> shader_lights;
            Vector<RenderShadowLight> render_shadow_lights;
            uint4 forward_light_mask;
            uint2 shadow_map_atlas_size = { 0, 0 };

            void Clear()
            {
                shader_instances.clear();
                shader_geometries.clear();
                shader_materials.clear();
                renderables.clear();
                shader_lights.clear();
                forward_light_mask = { 0,0,0,0 };
                shadow_map_atlas_size = { 0, 0 };
                render_shadow_lights.clear();
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
        Vector<std::shared_ptr<System>> systems;
        Vector<Vector<uint32>> system_execution_batches;
        bool system_schedule_dirty = true;
    };
}
