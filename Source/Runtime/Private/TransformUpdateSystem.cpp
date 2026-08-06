#include "TransformUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "JobSystem.h"

namespace won::ecs
{
    void TransformUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();

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
            hierarchy_children_cache.clear();

            Vector<Entity> stack;

            for (Size i = 0; i < hierarchy_array->GetSize(); ++i)
            {
                Entity entity = hierarchy_array->index_to_entity[i];
                Entity parent_id = hierarchy_array->data[i].parent_id;

                if (parent_id != INVALID_ENTITY && hierarchy_array->HasData(parent_id))
                {
                    hierarchy_children_cache[parent_id].push_back(entity);
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

                auto it = hierarchy_children_cache.find(current);
                if (it != hierarchy_children_cache.end())
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
        auto layout_array = scene.GetComponentArray<LayoutComponent>().get();

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
                const bool parent_has_layout = parent_id != INVALID_ENTITY && layout_array && layout_array->HasData(parent_id);

                if (!parent_has_layout)
                {
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

                if (layout_array && layout_array->HasData(entity))
                {
                    auto layout_it = hierarchy_children_cache.find(entity);
                    if (layout_it != hierarchy_children_cache.end())
                    {
                        const LayoutComponent& layout = layout_array->GetData(entity);
                        const Vector<Entity>& children = layout_it->second;
                        const bool horizontal = layout.type == LayoutComponent::Type::Horizontal;
                        const float2 inner_min = { rect.resolved_position.x + layout.padding_min.x, rect.resolved_position.y + layout.padding_min.y };
                        float2 inner_size = { rect.resolved_size.x - layout.padding_min.x - layout.padding_max.x, rect.resolved_size.y - layout.padding_min.y - layout.padding_max.y };
                        if (inner_size.x < 0.0f) { inner_size.x = 0.0f; }
                        if (inner_size.y < 0.0f) { inner_size.y = 0.0f; }
                        const float inner_cross = horizontal ? inner_size.y : inner_size.x;
                        const float inner_cross_min = horizontal ? inner_min.y : inner_min.x;
                        float cursor = horizontal ? inner_min.x : inner_min.y;
                        const Size count = children.size();
                        for (Size k = 0; k < count; ++k)
                        {
                            const Entity c = layout.reverse ? children[count - 1 - k] : children[k];
                            if (!rect_array->HasData(c))
                            {
                                continue;
                            }
                            RectTransform2DComponent& child = rect_array->GetData(c);
                            const float child_main = horizontal ? child.size.x : child.size.y;
                            const float own_cross = horizontal ? child.size.y : child.size.x;
                            float child_cross = own_cross;
                            float cross_off = 0.0f;
                            switch (layout.cross_align)
                            {
                            case LayoutComponent::CrossAlign::Stretch: child_cross = inner_cross; break;
                            case LayoutComponent::CrossAlign::Center:  cross_off = (inner_cross - own_cross) * 0.5f; break;
                            case LayoutComponent::CrossAlign::End:     cross_off = inner_cross - own_cross; break;
                            default: break;
                            }
                            if (horizontal)
                            {
                                child.resolved_position = { cursor, inner_cross_min + cross_off };
                                child.resolved_size = { child_main, child_cross };
                            }
                            else
                            {
                                child.resolved_position = { inner_cross_min + cross_off, cursor };
                                child.resolved_size = { child_cross, child_main };
                            }
                            child.reference_resolution = rect.reference_resolution;
                            child.layer_mask = rect.layer_mask;
                            child.match = rect.match;
                            child.SetDirty(false);
                            cursor += child_main + layout.spacing;
                        }
                    }
                }
            }
        }

        auto geometry_array = scene.GetComponentArray<GeometryComponent>().get();

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
        });

        if (dirty.load() == true)
        {
            scene.SetBVHDirty(true);
        }
        
        jobsystem::Wait(sub_ctx);
        
    }
}
