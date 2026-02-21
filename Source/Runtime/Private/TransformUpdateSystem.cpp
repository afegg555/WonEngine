#include "TransformUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "TransformComponent.h"
#include "HierarchyComponent.h"
#include "JobSystem.h"

namespace won::ecs
{
    static constexpr uint32_t groupsize = 256u;

    void TransformUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        jobsystem::Dispatch(sub_ctx, (uint32_t)transform_array->data.size(), groupsize, [&](jobsystem::JobArgs args) {

            TransformComponent& transform = transform_array->data[args.job_index];
            transform.UpdateTransform();
            });

        jobsystem::Wait(sub_ctx); // dependencies

        auto hierarchy_array = scene.GetComponentArray<HierarchyComponent>().get();
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
    }
}