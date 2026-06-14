#include "PhysicsUpdateSystem.h"
#include "Collider3DComponent.h"
#include "Rigidbody3DComponent.h"
#include "TransformComponent.h"
#include "HierarchyComponent.h"
#include "Scene.h"
#include "PhysicsWorld.h"
#include "JobSystem.h"

#include <unordered_set>
#include <algorithm>
#include <cmath>

using namespace DirectX;

namespace won::ecs
{
    void PhysicsUpdateSystem::Update(Scene& scene, float delta_time)
    {
        auto collider_array = scene.GetComponentArray<Collider3DComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        auto rigidbody_array = scene.GetComponentArray<Rigidbody3DComponent>().get();
        auto hierarchy_array = scene.GetComponentArray<HierarchyComponent>().get();

        physics::PhysicsWorld* physics_world = scene.GetPhysicsWorld();
        if (!physics_world || !collider_array || !transform_array)
        {
            return;
        }

        // 1. Reconciliation: Create Jolt bodies for new entities, Recreate if dirty, remove if gone

        std::unordered_set<Entity> current_entities;
        for (Size i = 0; i < collider_array->GetSize(); ++i)
        {
            const Entity entity = collider_array->index_to_entity[i];
            current_entities.insert(entity);

            bool has_rb = rigidbody_array && rigidbody_array->HasData(entity);
            Rigidbody3DComponent* rb = has_rb ? &rigidbody_array->GetData(entity) : nullptr;

            if (!physics_world->HasBody(entity))
            {
                physics_world->AddBody(entity, transform_array->GetData(entity), collider_array->data[i], rb);
            }
            else
            {
                physics_world->UpdateBody(entity, transform_array->GetData(entity), collider_array->data[i], rb);
            }
        }

        // Clean up Jolt bodies for entities that no longer have a collider component
        physics_world->CleanupBodies(current_entities);

        // 2. Sync manual transform changes (or Kinematic movement) from ECS to Jolt
        for (Size i = 0; i < collider_array->GetSize(); ++i)
        {
            const Entity entity = collider_array->index_to_entity[i];
            if (physics_world->IsAdded(entity))
            {
                physics_world->SyncTransformToPhysics(entity, transform_array->GetData(entity), collider_array->data[i]);
            }
        }

        // 3. Step physics simulation
        physics_world->Step(delta_time);

        // 4. Sync Jolt results back to ECS transforms for dynamic bodies
        for (Size i = 0; i < collider_array->GetSize(); ++i)
        {
            const Entity entity = collider_array->index_to_entity[i];
            if (physics_world->IsAdded(entity) && physics_world->IsDynamic(entity))
            {
                TransformComponent& transform = transform_array->GetData(entity);
                const Collider3DComponent& collider = collider_array->data[i];

                float3 body_pos;
                float4 body_rot;
                physics_world->GetBodyTransform(entity, body_pos, body_rot);

                XMVECTOR jolt_pos = XMVectorSet(body_pos.x, body_pos.y, body_pos.z, 1.0f);
                XMVECTOR jolt_rot = XMVectorSet(body_rot.x, body_rot.y, body_rot.z, body_rot.w);

                XMMATRIX body_matrix = XMMatrixRotationQuaternion(jolt_rot) * XMMatrixTranslationFromVector(jolt_pos);
                XMMATRIX offset_matrix = XMMatrixTranslation(-collider.offset.x, -collider.offset.y, -collider.offset.z);
                XMMATRIX entity_world = offset_matrix * body_matrix;

                XMMATRIX entity_local = entity_world;
                if (hierarchy_array && hierarchy_array->HasData(entity))
                {
                    Entity parent = hierarchy_array->GetData(entity).parent_id;
                    if (parent != INVALID_ENTITY && transform_array->HasData(parent))
                    {
                        XMMATRIX parent_world = transform_array->GetData(parent).GetWorldTransform();
                        XMMATRIX inv_parent = XMMatrixInverse(nullptr, parent_world);
                        entity_local = entity_world * inv_parent;
                    }
                }

                XMVECTOR S, R, T;
                if (XMMatrixDecompose(&S, &R, &T, entity_local))
                {
                    XMStoreFloat3(&transform.position, T);
                    XMStoreFloat4(&transform.rotation, R);
                    transform.SetDirty();
                    transform.UpdateTransform();
                }

                if (rigidbody_array && rigidbody_array->HasData(entity))
                {
                    Rigidbody3DComponent& rb = rigidbody_array->GetData(entity);
                    float3 lin_vel;
                    float3 ang_vel;
                    physics_world->GetBodyVelocity(entity, lin_vel, ang_vel);
                    rb.linear_velocity = lin_vel;
                    rb.angular_velocity = ang_vel;
                }
            }
        }

        // 5. Update ECS Collider bounds using the new world transform
        uint32_t groupsize = 64;
        jobsystem::Context bounds_ctx;
        jobsystem::Dispatch(bounds_ctx, static_cast<uint32_t>(collider_array->GetSize()), groupsize, [&](jobsystem::JobArgs args) {
            const Entity entity = collider_array->index_to_entity[args.job_index];
            Collider3DComponent& collider = collider_array->data[args.job_index];

            collider.world_bounds.Invalidate();
            collider.world_sphere = {};
            if (!transform_array->HasData(entity))
            {
                return;
            }

            const TransformComponent& transform = transform_array->GetData(entity);
            const XMMATRIX world = transform.GetWorldTransform();

            if (collider.shape_type == Collider3DComponent::Sphere)
            {
                const XMVECTOR center = XMVector3TransformCoord(XMLoadFloat3(&collider.offset), world);
                const float scale_x = XMVectorGetX(XMVector3Length(world.r[0]));
                const float scale_y = XMVectorGetX(XMVector3Length(world.r[1]));
                const float scale_z = XMVectorGetX(XMVector3Length(world.r[2]));
                const float max_scale = (std::max)((std::max)(scale_x, scale_y), scale_z);

                XMStoreFloat3(&collider.world_sphere.center, center);
                collider.world_sphere.radius = (std::max)(0.0f, collider.radius) * max_scale;
                const float3 half_width = { collider.world_sphere.radius, collider.world_sphere.radius, collider.world_sphere.radius };
                collider.world_bounds.CreateFromHalfWidth(collider.world_sphere.center, half_width);
            }
            else
            {
                math::AABB local_bounds = {};
                const float3 half_extent = {
                    (std::max)(0.0f, collider.half_extent.x),
                    (std::max)(0.0f, collider.half_extent.y),
                    (std::max)(0.0f, collider.half_extent.z)
                };
                local_bounds.CreateFromHalfWidth(collider.offset, half_extent);
                collider.world_bounds = local_bounds.TransformAABB(world);

                collider.world_sphere.center = collider.world_bounds.GetCenter();
                const float3 extent = collider.world_bounds.GetExtent();
                collider.world_sphere.radius = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
            }

            collider.SetDirty(false);
        });
        jobsystem::Wait(bounds_ctx);

        scene.SetBVHDirty(true);
    }
}
