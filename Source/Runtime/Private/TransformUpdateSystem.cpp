#include "TransformUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "JobSystem.h"
#include <mutex>

namespace won::ecs
{
    void TransformUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        Scene::RenderData& render_data = scene.GetRenderData();

        std::atomic<bool> dirty(false);
        // update local transform
        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->data.size(), groupsize, [&](jobsystem::JobArgs args) {

            TransformComponent& transform = transform_array->data[args.job_index];
            if (transform.IsDirty())
            {
                dirty.store(true);
                transform.UpdateTransform();
            }
            
            });

        auto hierarchy_array = scene.GetComponentArray<HierarchyComponent>().get();

        // rebuild update order cache if scene topology is dirty
        if (scene.IsHierarchyTopologyDirty())
        {
            hierarchy_update_order_cache.clear();

            UnorderedMap<Entity, Vector<Entity>> parent_to_children;
            Vector<Entity> roots;

            for (Size i = 0; i < hierarchy_array->GetSize(); ++i)
            {
                Entity entity = hierarchy_array->index_to_entity[i];
                Entity parent_id = hierarchy_array->data[i].parent_id;

                if (parent_id != INVALID_ENTITY && transform_array->HasData(parent_id) && hierarchy_array->HasData(parent_id))
                {
                    parent_to_children[parent_id].push_back(entity);
                }
                else
                {
                    roots.push_back(entity);
                }
            }

            // DFS hierarchy tree traversal
            hierarchy_update_order_cache.reserve(hierarchy_array->GetSize());
            Vector<Entity> stack = std::move(roots);
            while (!stack.empty())
            {
                Entity current = stack.back();
                stack.pop_back();

                hierarchy_update_order_cache.push_back(current);

                auto it = parent_to_children.find(current);
                if (it != parent_to_children.end())
                {
                    for (Entity child : it->second)
                    {
                        stack.push_back(child);
                    }
                }
            }

            scene.SetHierarchyTopologyDirty(false);
        }

        jobsystem::Wait(sub_ctx); // wait for local transforms to complete before propagating to world transforms

        // update world transform using topologically sorted order (flat linear loop)
        for (Entity entity : hierarchy_update_order_cache)
        {
            Entity parent_id = hierarchy_array->GetData(entity).parent_id;
            if (parent_id != INVALID_ENTITY && transform_array->HasData(parent_id))
            {
                TransformComponent& transform = transform_array->GetData(entity);
                XMMATRIX parent_world = transform_array->GetData(parent_id).GetWorldTransform();
                XMMATRIX local = transform.GetLocalTransform();

                XMStoreFloat4x4(&transform.world_transform, local * parent_world);
            }
        }

        auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();

        render_data.shadow_caster_world_bound.Invalidate();
        // update world bounds
        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->data.size(), groupsize, [&](jobsystem::JobArgs args) {
            TransformComponent& transform = transform_array->data[args.job_index];
            Entity entity = transform_array->index_to_entity[args.job_index];

            transform.world_bounds.Invalidate();
            if (!geometry_array->HasData(entity))
            {
                return;
            }

            const GeometryComponent& geometry = geometry_array->GetData(entity);
            transform.world_bounds = geometry.local_bounds.TransformAABB(transform.world_transform);

            if (geometry.IsCastShadow())
            {
                std::lock_guard<std::mutex> mutex(shadow_caster_world_bound_mutex);
                render_data.shadow_caster_world_bound.Merge(transform.world_bounds);
            }
        });

        if (dirty.load() == true)
        {
            scene.SetBVHDirty(true);
        }
        
        jobsystem::Wait(sub_ctx);
        
    }
}
