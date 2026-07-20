#include "PhysicsUpdateSystem.h"
#include "Scene.h"
#include "PhysicsWorld.h"
#include "TerrainGenerator.h"
#include "JobSystem.h"

using namespace DirectX;

namespace won::ecs
{
    void PhysicsUpdateSystem::Update(Scene& scene, float delta_time)
    {
        auto collider_array = scene.GetComponentArray<Collider3DComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        auto rigidbody_array = scene.GetComponentArray<Rigidbody3DComponent>().get();
        auto collision_layer_array = scene.GetComponentArray<CollisionLayerComponent>().get();
        auto hierarchy_array = scene.GetComponentArray<HierarchyComponent>().get();

        physics::PhysicsWorld* physics_world = scene.GetPhysicsWorld();
        if (!physics_world || !collider_array || !transform_array)
        {
            return;
        }

        auto add_body = [&](Entity entity, TransformComponent& transform, Collider3DComponent& collider, Rigidbody3DComponent* rb, uint32_t collision_layer)
        {
            if (collider.shape_type == Collider3DComponent::ShapeType::HeightField)
            {
                TerrainComponent* terrain = scene.GetComponent<TerrainComponent>(entity);
                if (!terrain)
                {
                    return;
                }
                const TerrainHeightField height_field = GenerateTerrainHeights(*terrain);
                physics::PhysicsWorld::HeightFieldShapeDesc desc = {};
                desc.samples = height_field.heights.data();
                desc.samples_x = height_field.samples_x;
                desc.samples_z = height_field.samples_z;
                desc.cell_x = height_field.cell_x;
                desc.cell_z = height_field.cell_z;
                desc.offset_x = height_field.offset_x;
                desc.offset_z = height_field.offset_z;
                physics_world->AddBody(entity, transform, collider, rb, collision_layer, &desc);
                return;
            }
            physics_world->AddBody(entity, transform, collider, rb, collision_layer);
        };

        jobsystem::Context sub_ctx;
        jobsystem::Dispatch(sub_ctx, (uint32_t)collider_array->GetSize(), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
        {
            const Entity entity = collider_array->index_to_entity[args.job_index];

            TransformComponent& transform = transform_array->GetData(entity);
            Collider3DComponent& collider = collider_array->data[args.job_index];
            Rigidbody3DComponent* rb = (rigidbody_array && rigidbody_array->HasData(entity)) ? &rigidbody_array->GetData(entity) : nullptr;
            const uint32_t collision_layer = (collision_layer_array && collision_layer_array->HasData(entity)) ? collision_layer_array->GetData(entity).layer : 0;

            if (!physics_world->HasBody(entity))
            {
                add_body(entity, transform, collider, rb, collision_layer);
            }
            else if (collider.IsDirty() || (rb && rb->IsDirty()))
            {
                physics_world->RemoveBody(entity);
                add_body(entity, transform, collider, rb, collision_layer);
            }
            else
            {
				physics_world->SetBodyCollisionLayer(entity, collision_layer);
				physics_world->SyncTransformToPhysics(entity, transform, collider); // sync kinematic / static body transform
            }
        });
        jobsystem::Wait(sub_ctx);

        // step physics simulation
        physics_world->Step(delta_time);

        jobsystem::Context post_ctx;
        jobsystem::Dispatch(post_ctx, (uint32_t)collider_array->GetSize(), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
        {
            const Entity entity = collider_array->index_to_entity[args.job_index];
            Collider3DComponent& collider = collider_array->data[args.job_index];

            if (physics_world->HasBody(entity) && physics_world->IsDynamic(entity))
            {
                TransformComponent& transform = transform_array->GetData(entity);


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

            // Update Collider Bounds
            if (transform_array->HasData(entity))
            {
                const TransformComponent& transform = transform_array->GetData(entity);
                const XMMATRIX world = transform.GetWorldTransform();

                collider.world_bounds.Invalidate();
                collider.world_sphere = {};

                if (collider.shape_type == Collider3DComponent::ShapeType::Sphere)
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
            }
        });
        jobsystem::Wait(post_ctx);

        scene.SetBVHDirty(true);
    }
}
