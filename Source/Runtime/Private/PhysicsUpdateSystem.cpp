#include "PhysicsUpdateSystem.h"
#include "Backlog.h"
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
				physics_world->ApplySceneTransformToBody(entity, transform, collider);
                if (rb && rb->IsVelocityDirty())
                {
                    physics_world->SetBodyVelocity(entity, rb->linear_velocity, rb->angular_velocity);
                    rb->SetVelocityDirty(false);
                }
            }
        });
        jobsystem::Wait(sub_ctx);

        auto joint_array = scene.GetComponentArray<JointComponent>().get();
        if (joint_array && joint_array->GetSize() > 0)
        {
            jobsystem::Dispatch(sub_ctx, (uint32_t)joint_array->GetSize(), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
            {
                const Entity entity = joint_array->index_to_entity[args.job_index];
                JointComponent& joint = joint_array->data[args.job_index];

                if (!joint.IsEnabled())
                {
                    if (physics_world->HasJoint(entity))
                    {
                        physics_world->RemoveJoint(entity);
                    }
                    return;
                }

                if (joint.IsDirty() && physics_world->HasJoint(entity))
                {
                    physics_world->RemoveJoint(entity);
                }

                if (physics_world->HasJoint(entity))
                {
                    return;
                }

                if (!physics_world->HasBody(entity))
                {
                    return;
                }
                if (joint.connected_entity != INVALID_ENTITY && !physics_world->HasBody(joint.connected_entity))
                {
                    return;
                }

                physics_world->AddJoint(entity, joint);
                joint.SetDirty(false);
            });
            jobsystem::Wait(sub_ctx);
        }

        auto soft_body_array = scene.GetComponentArray<SoftBodyComponent>().get();
        if (soft_body_array && soft_body_array->GetSize() > 0)
        {
            jobsystem::Dispatch(sub_ctx, static_cast<uint32>(soft_body_array->GetSize()), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
            {
                const Size soft_body_index = args.job_index;
                const Entity entity = soft_body_array->index_to_entity[soft_body_index];
                SoftBodyComponent& soft_body = soft_body_array->data[soft_body_index];

                if (!soft_body.IsEnabled())
                {
                    if (physics_world->HasBody(entity))
                    {
                        physics_world->RemoveBody(entity);
                    }
                    return;
                }

                if (!transform_array->HasData(entity))
                {
                    return;
                }

                if (physics_world->HasBody(entity) && soft_body.IsDirty())
                {
                    physics_world->RemoveBody(entity);
                }

                if (!physics_world->HasBody(entity))
                {
                    const GeometryComponent* geometry = scene.GetComponent<GeometryComponent>(entity);
                    if (!geometry || !geometry->mesh)
                    {
                        return;
                    }

                    const uint32_t collision_layer = (collision_layer_array && collision_layer_array->HasData(entity)) ? collision_layer_array->GetData(entity).layer : 0;
                    physics_world->AddSoftBody(entity, transform_array->GetData(entity), soft_body, *geometry->mesh, collision_layer);
                }
            });
            jobsystem::Wait(sub_ctx);
        }

        auto vehicle_array = scene.GetComponentArray<VehicleComponent>().get();
        if (vehicle_array && vehicle_array->GetSize() > 0)
        {
            jobsystem::Dispatch(sub_ctx, static_cast<uint32>(vehicle_array->GetSize()), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
            {
                const Size vehicle_index = args.job_index;
                const Entity entity = vehicle_array->index_to_entity[vehicle_index];
                VehicleComponent& vehicle = vehicle_array->data[vehicle_index];

                if (!vehicle.IsEnabled())
                {
                    if (physics_world->HasVehicle(entity))
                    {
                        physics_world->RemoveVehicle(entity);
                    }
                    return;
                }

                if (vehicle.IsDirty() && physics_world->HasVehicle(entity))
                {
                    physics_world->RemoveVehicle(entity);
                }

                if (!physics_world->HasVehicle(entity))
                {
                    if (!physics_world->HasBody(entity) || !physics_world->IsDynamic(entity))
                    {
                        return;
                    }

                    if (transform_array->HasData(entity))
                    {
                        XMVECTOR scale_vec, rotation_vec, position_vec;
                        XMMatrixDecompose(&scale_vec, &rotation_vec, &position_vec, transform_array->GetData(entity).GetWorldTransform());
                        float3 scale;
                        XMStoreFloat3(&scale, scale_vec);
                        if (std::abs(scale.x - 1.0f) > 1e-3f || std::abs(scale.y - 1.0f) > 1e-3f || std::abs(scale.z - 1.0f) > 1e-3f)
                        {
                            wonlog_warning("[PhysicsUpdateSystem] vehicle entity %llu has world scale %.3f %.3f %.3f, wheel sizes ignore transform scale", static_cast<unsigned long long>(entity), scale.x, scale.y, scale.z);
                        }
                    }

                    physics_world->AddVehicle(entity, vehicle);
                    if (!physics_world->HasVehicle(entity))
                    {
                        return;
                    }
                    vehicle.SetDirty(false);
                }

                physics_world->SetVehicleInput(entity, vehicle.throttle_input, vehicle.steer_input, vehicle.brake_input, vehicle.hand_brake_input);
            });
            jobsystem::Wait(sub_ctx);
        }

        // step physics simulation
        physics_world->Step(delta_time);

        if (vehicle_array && vehicle_array->GetSize() > 0)
        {
            jobsystem::Context vehicle_state_ctx;
            jobsystem::Dispatch(vehicle_state_ctx, static_cast<uint32>(vehicle_array->GetSize()), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
            {
                const Size vehicle_index = args.job_index;
                const Entity entity = vehicle_array->index_to_entity[vehicle_index];
                VehicleComponent& vehicle = vehicle_array->data[vehicle_index];
                physics_world->ReadVehicleState(entity, vehicle);

                if (!physics_world->HasVehicle(entity))
                {
                    return;
                }

                XMMATRIX chassis_world = XMMatrixIdentity();
                if (transform_array->HasData(entity))
                {
                    float3 body_pos;
                    float4 body_rot;
                    physics_world->GetSimulatedBodyTransform(entity, body_pos, body_rot);
                    const XMMATRIX body_matrix = XMMatrixRotationQuaternion(XMVectorSet(body_rot.x, body_rot.y, body_rot.z, body_rot.w)) * XMMatrixTranslation(body_pos.x, body_pos.y, body_pos.z);
                    const float3 offset = collider_array->HasData(entity) ? collider_array->GetData(entity).offset : float3(0.0f, 0.0f, 0.0f);
                    chassis_world = XMMatrixTranslation(-offset.x, -offset.y, -offset.z) * body_matrix;
                }

                for (const VehicleWheel& wheel : vehicle.wheels)
                {
                    if (wheel.visual_entity == INVALID_ENTITY || !transform_array->HasData(wheel.visual_entity))
                    {
                        continue;
                    }

                    const XMMATRIX wheel_world = XMMatrixRotationQuaternion(XMVectorSet(wheel.world_rotation.x, wheel.world_rotation.y, wheel.world_rotation.z, wheel.world_rotation.w))
                        * XMMatrixTranslation(wheel.world_position.x, wheel.world_position.y, wheel.world_position.z);

                    XMMATRIX parent_world = XMMatrixIdentity();
                    if (hierarchy_array && hierarchy_array->HasData(wheel.visual_entity))
                    {
                        const Entity parent = hierarchy_array->GetData(wheel.visual_entity).parent_id;
                        if (parent == entity)
                        {
                            parent_world = chassis_world;
                        }
                        else if (parent != INVALID_ENTITY && transform_array->HasData(parent))
                        {
                            parent_world = transform_array->GetData(parent).GetWorldTransform();
                        }
                    }

                    const XMMATRIX wheel_local = wheel_world * XMMatrixInverse(nullptr, parent_world);
                    XMVECTOR S, R, T;
                    if (XMMatrixDecompose(&S, &R, &T, wheel_local))
                    {
                        TransformComponent& wheel_transform = transform_array->GetData(wheel.visual_entity);
                        XMStoreFloat3(&wheel_transform.position, T);
                        XMStoreFloat4(&wheel_transform.rotation, R);
                        wheel_transform.SetDirty();
                    }
                }
            });
            jobsystem::Wait(vehicle_state_ctx);
        }

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
                physics_world->GetInterpolatedBodyTransform(entity, body_pos, body_rot);

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
                    physics_world->SetLastPhysicsOutputTransform(entity, transform.position, transform.rotation);
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
                else if (collider.shape_type == Collider3DComponent::ShapeType::HeightField)
                {
                    const GeometryComponent* geometry = scene.GetComponent<GeometryComponent>(entity);
                    if (geometry && geometry->local_bounds.IsValid())
                    {
                        collider.world_bounds = geometry->local_bounds.TransformAABB(world);
                        collider.world_sphere.center = collider.world_bounds.GetCenter();
                        const float3 extent = collider.world_bounds.GetExtent();
                        collider.world_sphere.radius = std::sqrt(extent.x * extent.x + extent.y * extent.y + extent.z * extent.z);
                    }
                }
				else // Box
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
