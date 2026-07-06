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
            Vector<Entity> stack;

            for (Size i = 0; i < hierarchy_array->GetSize(); ++i)
            {
                Entity entity = hierarchy_array->index_to_entity[i];
                Entity parent_id = hierarchy_array->data[i].parent_id;

                if (parent_id != INVALID_ENTITY && hierarchy_array->HasData(parent_id))
                {
                    parent_to_children[parent_id].push_back(entity);
                }
                else
                {
                    stack.push_back(entity);
                }
            }

            // DFS hierarchy tree traversal
            hierarchy_update_order_cache.reserve(hierarchy_array->GetSize());
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

        auto rect_array = scene.GetComponentArray<RectTransform2DComponent>().get();
        auto canvas_array = scene.GetComponentArray<Canvas2DComponent>().get();

        for (Entity entity : hierarchy_update_order_cache)
        {
            Entity parent_id = hierarchy_array->GetData(entity).parent_id;

            if (parent_id != INVALID_ENTITY && transform_array->HasData(parent_id) && transform_array->HasData(entity))
            {
                TransformComponent& transform = transform_array->GetData(entity);
                XMMATRIX parent_world = transform_array->GetData(parent_id).GetWorldTransform();
                XMMATRIX local = transform.GetLocalTransform();

                XMStoreFloat4x4(&transform.world_transform, local * parent_world);
            }

            if (rect_array && rect_array->HasData(entity))
            {
                RectTransform2DComponent& rect = rect_array->GetData(entity);

                float2 parent_position = { 0.0f, 0.0f };
                float2 parent_size = { 1920.0f, 1080.0f };
                float2 reference = { 0.0f, 0.0f };
                uint32 layer_mask = 0xFFFFFFFF;
                float match = 0.5f;
                if (parent_id != INVALID_ENTITY && rect_array->HasData(parent_id))
                {
                    const RectTransform2DComponent& parent_rect = rect_array->GetData(parent_id);
                    parent_position = parent_rect.resolved_position;
                    parent_size = parent_rect.resolved_size;
                    reference = parent_rect.reference_resolution;
                    layer_mask = parent_rect.layer_mask;
                    match = parent_rect.match;
                }
                else if (parent_id != INVALID_ENTITY && canvas_array && canvas_array->HasData(parent_id))
                {
                    const Canvas2DComponent& canvas = canvas_array->GetData(parent_id);
                    parent_size = canvas.reference_resolution;
                    reference = (canvas.scale_mode == UIScaleMode::ScaleWithScreenSize) ? canvas.reference_resolution : float2{ 0.0f, 0.0f };
                    layer_mask = canvas.layer_mask;
                    match = canvas.match;
                }

                const float2 pivot_point = {
                    parent_position.x + rect.anchor.x * parent_size.x + rect.position.x,
                    parent_position.y + rect.anchor.y * parent_size.y + rect.position.y
                };
                rect.resolved_position = { pivot_point.x - rect.pivot.x * rect.size.x, pivot_point.y - rect.pivot.y * rect.size.y };
                rect.resolved_size = rect.size;
                rect.reference_resolution = reference;
                rect.layer_mask = layer_mask;
                rect.match = match;
                rect.SetDirty(false);
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
