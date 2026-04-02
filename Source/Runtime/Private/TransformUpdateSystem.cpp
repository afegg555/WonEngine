#include "TransformUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "TransformComponent.h"
#include "HierarchyComponent.h"
#include "GeometryComponent.h"
#include "JobSystem.h"
#include <mutex>

namespace won::ecs
{
    static std::mutex shadow_caster_world_bound_mutex;

    void TransformUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        Scene::RenderData& render_data = scene.GetRenderData();

        // update local transform
        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->data.size(), groupsize, [&](jobsystem::JobArgs args) {

            TransformComponent& transform = transform_array->data[args.job_index];
            transform.UpdateTransform();
            });

        jobsystem::Wait(sub_ctx); // dependencies

        auto hierarchy_array = scene.GetComponentArray<HierarchyComponent>().get();

        // update world transform using hierarchy
        jobsystem::Dispatch(sub_ctx, (uint32_t)hierarchy_array->data.size(), groupsize, [&](jobsystem::JobArgs args) {

            HierarchyComponent& hierarchy = hierarchy_array->data[args.job_index];
            Entity entity = hierarchy_array->index_to_entity[args.job_index];

            TransformComponent& transform_child = transform_array->GetData(entity);
            XMMATRIX worldmatrix = transform_child.GetLocalTransform();

            Entity parent_id = hierarchy.parent_id;
            while (parent_id != INVALID_ENTITY)
            {
                TransformComponent& transform_parent = transform_array->GetData(parent_id);
                worldmatrix *= transform_parent.GetLocalTransform();

                if (hierarchy_array->HasData(parent_id))
                {
                    HierarchyComponent& hier_recursive = hierarchy_array->GetData(parent_id);
                    parent_id = hier_recursive.parent_id;
                }
                else
                {
                    parent_id = INVALID_ENTITY;
                }
            }
            XMStoreFloat4x4(&transform_child.world_transform, worldmatrix);
            });
        jobsystem::Wait(sub_ctx);

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
            transform.world_bounds = math::TransformAABB(geometry.local_bounds, transform.world_transform);

            if (geometry.IsCastShadow())
            {
                std::lock_guard<std::mutex> mutex(shadow_caster_world_bound_mutex);
                render_data.shadow_caster_world_bound.Merge(transform.world_bounds);
            }
        });
        jobsystem::Wait(sub_ctx);
    }
}
