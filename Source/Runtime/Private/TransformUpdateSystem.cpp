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
                    float2 parent_anchor_min = { 0.0f, 0.0f };
                    float2 parent_anchor_max = { 1.0f, 1.0f };
                    float2 parent_offset_min = { 0.0f, 0.0f };
                    float2 parent_offset_max = { 0.0f, 0.0f };
                    float2 reference = { 0.0f, 0.0f };
                    uint32 layer_mask = 0xFFFFFFFF;
                    float match = 0.5f;
                    if (parent_id != INVALID_ENTITY && rect_array->HasData(parent_id))
                    {
                        const RectTransform2DComponent& parent_rect = rect_array->GetData(parent_id);
                        parent_anchor_min = parent_rect.resolved_anchor_min;
                        parent_anchor_max = parent_rect.resolved_anchor_max;
                        parent_offset_min = parent_rect.resolved_offset_min;
                        parent_offset_max = parent_rect.resolved_offset_max;
                        reference = parent_rect.reference_resolution;
                        layer_mask = parent_rect.layer_mask;
                        match = parent_rect.match;
                    }
                    else if (parent_id != INVALID_ENTITY && canvas_array && canvas_array->HasData(parent_id))
                    {
                        const Canvas2DComponent& canvas = canvas_array->GetData(parent_id);
                        reference = (canvas.scale_mode == UIScaleMode::ScaleWithScreenSize) ? canvas.reference_resolution : float2{ 0.0f, 0.0f };
                        layer_mask = canvas.layer_mask;
                        match = canvas.match;
                    }

                    const float2 anchor_frac_min = {
                        parent_anchor_min.x + (parent_anchor_max.x - parent_anchor_min.x) * rect.anchor_min.x,
                        parent_anchor_min.y + (parent_anchor_max.y - parent_anchor_min.y) * rect.anchor_min.y
                    };
                    const float2 anchor_frac_max = {
                        parent_anchor_min.x + (parent_anchor_max.x - parent_anchor_min.x) * rect.anchor_max.x,
                        parent_anchor_min.y + (parent_anchor_max.y - parent_anchor_min.y) * rect.anchor_max.y
                    };
                    const float2 anchor_off_min = {
                        parent_offset_min.x + (parent_offset_max.x - parent_offset_min.x) * rect.anchor_min.x,
                        parent_offset_min.y + (parent_offset_max.y - parent_offset_min.y) * rect.anchor_min.y
                    };
                    const float2 anchor_off_max = {
                        parent_offset_min.x + (parent_offset_max.x - parent_offset_min.x) * rect.anchor_max.x,
                        parent_offset_min.y + (parent_offset_max.y - parent_offset_min.y) * rect.anchor_max.y
                    };

                    rect.resolved_anchor_min = anchor_frac_min;
                    rect.resolved_anchor_max = anchor_frac_max;
                    rect.resolved_offset_min = {
                        anchor_off_min.x + rect.anchored_position.x - rect.pivot.x * rect.size_delta.x,
                        anchor_off_min.y + rect.anchored_position.y - rect.pivot.y * rect.size_delta.y
                    };
                    rect.resolved_offset_max = {
                        anchor_off_max.x + rect.anchored_position.x + (1.0f - rect.pivot.x) * rect.size_delta.x,
                        anchor_off_max.y + rect.anchored_position.y + (1.0f - rect.pivot.y) * rect.size_delta.y
                    };
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
                        const float2 inner_min = { rect.resolved_offset_min.x + layout.padding_min.x, rect.resolved_offset_min.y + layout.padding_min.y };
                        float2 inner_size = { rect.resolved_offset_max.x - rect.resolved_offset_min.x - layout.padding_min.x - layout.padding_max.x, rect.resolved_offset_max.y - rect.resolved_offset_min.y - layout.padding_min.y - layout.padding_max.y };
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
                            const float child_main = horizontal ? child.size_delta.x : child.size_delta.y;
                            const float own_cross = horizontal ? child.size_delta.y : child.size_delta.x;
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
                                child.resolved_offset_min = { cursor, inner_cross_min + cross_off };
                                child.resolved_offset_max = { cursor + child_main, inner_cross_min + cross_off + child_cross };
                            }
                            else
                            {
                                child.resolved_offset_min = { inner_cross_min + cross_off, cursor };
                                child.resolved_offset_max = { inner_cross_min + cross_off + child_cross, cursor + child_main };
                            }
                            child.resolved_anchor_min = rect.resolved_anchor_min;
                            child.resolved_anchor_max = rect.resolved_anchor_min;
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
