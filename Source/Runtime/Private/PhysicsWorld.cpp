#include "PhysicsWorld.h"
#include "Primitives.h"
#include "TransformComponent.h"
#include "Collider3DComponent.h"
#include "Rigidbody3DComponent.h"
#include "JobSystem.h"

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Collision/ContactListener.h>

#include <mutex>
#include <shared_mutex>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <array>

using namespace DirectX;

namespace won::physics
{
    static bool is_initialized = false;

    void Initialize()
    {
        if (is_initialized)
            return;

        std::printf("JPH::RegisterDefaultAllocator...\n"); std::fflush(stdout);
        JPH::RegisterDefaultAllocator();
        std::printf("JPH::Factory::sInstance...\n"); std::fflush(stdout);
        JPH::Factory::sInstance = new JPH::Factory();
        std::printf("JPH::RegisterTypes...\n"); std::fflush(stdout);
        JPH::RegisterTypes();
        std::printf("JPH initialized.\n"); std::fflush(stdout);
        is_initialized = true;
    }

    void Shutdown()
    {
        if (!is_initialized)
            return;

        if (JPH::Factory::sInstance)
        {
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
        JPH::UnregisterTypes();
        is_initialized = false;
    }

    namespace
    {
        std::array<uint32_t, 32> collision_matrix = {
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
            0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu
        };
    }

    void ResetCollisionMatrix()
    {
        collision_matrix.fill(0xFFFFFFFFu);
    }

    void SetLayerCollision(uint32_t layer_a, uint32_t layer_b, bool enabled)
    {
        if (layer_a >= 32 || layer_b >= 32)
        {
            return;
        }
        if (enabled)
        {
            collision_matrix[layer_a] |= (1u << layer_b);
            collision_matrix[layer_b] |= (1u << layer_a);
        }
        else
        {
            collision_matrix[layer_a] &= ~(1u << layer_b);
            collision_matrix[layer_b] &= ~(1u << layer_a);
        }
    }

    bool GetLayerCollision(uint32_t layer_a, uint32_t layer_b)
    {
        if (layer_a >= 32 || layer_b >= 32)
        {
            return false;
        }
        return (collision_matrix[layer_a] & (1u << layer_b)) != 0;
    }

    namespace Detail
    {
        namespace BroadPhaseLayers
        {
            static constexpr JPH::BroadPhaseLayer NON_MOVING(0);
            static constexpr JPH::BroadPhaseLayer MOVING(1);
            static constexpr JPH::BroadPhaseLayer TRIGGER(2);
            static constexpr JPH::uint NUM_LAYERS(3);
        };

        struct ObjectLayers
        {
            static constexpr JPH::ObjectLayer NON_MOVING = 0;
            static constexpr JPH::ObjectLayer MOVING = 1;
            static constexpr JPH::ObjectLayer TRIGGER = 2;
            static constexpr JPH::ObjectLayer NUM_LAYERS = 3;
        };

        class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface
        {
        public:
            BPLayerInterfaceImpl()
            {
                mObjectToBroadPhase[ObjectLayers::NON_MOVING] = BroadPhaseLayers::NON_MOVING;
                mObjectToBroadPhase[ObjectLayers::MOVING] = BroadPhaseLayers::MOVING;
                mObjectToBroadPhase[ObjectLayers::TRIGGER] = BroadPhaseLayers::TRIGGER;
            }

            JPH::uint GetNumBroadPhaseLayers() const override
            {
                return BroadPhaseLayers::NUM_LAYERS;
            }

            JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer inLayer) const override
            {
                assert(inLayer < ObjectLayers::NUM_LAYERS);
                return mObjectToBroadPhase[inLayer];
            }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
            const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer inLayer) const override
            {
                switch ((JPH::uint8)inLayer)
                {
                case 0:	return "NON_MOVING";
                case 1:	return "MOVING";
                case 2:	return "TRIGGER";
                default: return "INVALID";
                }
            }
#endif
        private:
            JPH::BroadPhaseLayer mObjectToBroadPhase[ObjectLayers::NUM_LAYERS];
        };

		class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter // broad phase layer filter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer inLayer1, JPH::BroadPhaseLayer inLayer2) const override
            {
                switch (inLayer1)
                {
                case ObjectLayers::NON_MOVING: // static
                    return inLayer2 == BroadPhaseLayers::MOVING || inLayer2 == BroadPhaseLayers::TRIGGER;
				case ObjectLayers::MOVING: // dynamic or kinematic
                    return true;
				case ObjectLayers::TRIGGER: // trigger
                    return inLayer2 == BroadPhaseLayers::NON_MOVING || inLayer2 == BroadPhaseLayers::MOVING;
                default:
                    return false;
                }
            }
        };

		class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter // narrow phase layer filter
        {
        public:
            bool ShouldCollide(JPH::ObjectLayer inObject1, JPH::ObjectLayer inObject2) const override
            {
                switch (inObject1)
                {
                case ObjectLayers::NON_MOVING: // static
                    return inObject2 == ObjectLayers::MOVING || inObject2 == ObjectLayers::TRIGGER;
                case ObjectLayers::MOVING: // dynamic or kinematic
                    return true;
                case ObjectLayers::TRIGGER: // trigger
					return inObject2 == ObjectLayers::NON_MOVING || inObject2 == ObjectLayers::MOVING; // !! trigger doesn't interact with trigger
                default:
                    return false;
                }
            }
        };

		// broad phase -> narrow phase -> generate contact
        class MyContactListener : public JPH::ContactListener
        {
        public:
            struct ContactPair {
                JPH::BodyID a;
                JPH::BodyID b;
                bool operator==(const ContactPair& o) const {
                    return (a == o.a && b == o.b) || (a == o.b && b == o.a);
                }
            };

            std::mutex mutex;
            Vector<ContactPair> active_pairs;

            void OnContactAdded(const JPH::Body& inBody1, const JPH::Body& inBody2, const JPH::ContactManifold& inManifold, JPH::ContactSettings& ioSettings) override
            {
                if (!inBody1.IsSensor() && !inBody2.IsSensor())
                    return;

                std::lock_guard<std::mutex> lock(mutex);
                ContactPair p = { inBody1.GetID(), inBody2.GetID() };
                if (std::find(active_pairs.begin(), active_pairs.end(), p) == active_pairs.end())
                {
                    active_pairs.push_back(p);
                }
            }

            void OnContactRemoved(const JPH::SubShapeIDPair& inSubShapePair) override
            {
                std::lock_guard<std::mutex> lock(mutex);
                ContactPair p = { inSubShapePair.GetBody1ID(), inSubShapePair.GetBody2ID() };
                auto it = std::find(active_pairs.begin(), active_pairs.end(), p);
                if (it != active_pairs.end())
                {
                    active_pairs.erase(it);
                }
            }

            JPH::ValidateResult OnContactValidate(const JPH::Body& inBody1, const JPH::Body& inBody2, JPH::RVec3Arg inBaseOffset, const JPH::CollideShapeResult& inCollisionResult) override
            {
                const uint32_t layer1 = static_cast<uint32_t>(inBody1.GetUserData());
                const uint32_t layer2 = static_cast<uint32_t>(inBody2.GetUserData());
                if (!won::physics::GetLayerCollision(layer1, layer2))
                {
                    return JPH::ValidateResult::RejectAllContactsForThisBodyPair;
                }
                return JPH::ValidateResult::AcceptAllContactsForThisBodyPair;
            }
        };
    }

    struct ActiveTriggerPair
    {
        won::ecs::Entity a = 0;
        won::ecs::Entity b = 0;
    };

    struct PhysicsWorldImpl
    {
        std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
        std::unique_ptr<JPH::JobSystemThreadPool> job_system;
        std::unique_ptr<JPH::PhysicsSystem> physics_system;

        Detail::BPLayerInterfaceImpl bp_layer_interface;
        Detail::ObjectVsBroadPhaseLayerFilterImpl object_vs_bp_filter;
        Detail::ObjectLayerPairFilterImpl object_pair_filter;
        Detail::MyContactListener contact_listener;

        mutable std::shared_mutex bodies_mutex;
        UnorderedMap<won::ecs::Entity, JPH::BodyID> entity_to_body;
        UnorderedMap<JPH::BodyID, won::ecs::Entity> body_to_entity;

        Vector<ActiveTriggerPair> active_trigger_pairs;
        Vector<Collider3DTriggerEvent> trigger_events;

        float accumulator = 0.0f;
        float fixed_step;
        int max_steps_per_frame;

        PhysicsWorldImpl(const PhysicsWorldDesc& desc)
        {
            fixed_step = 1.0f / desc.physics_hz;
            max_steps_per_frame = desc.max_steps_per_frame;

            Initialize();

            temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(desc.temp_allocator_size);
            job_system = std::make_unique<JPH::JobSystemThreadPool>(JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, std::thread::hardware_concurrency() - 1);
            physics_system = std::make_unique<JPH::PhysicsSystem>();

            physics_system->Init(
                desc.max_bodies,
                0,
                desc.max_body_pairs,
                desc.max_contact_constraints,
                bp_layer_interface,
                object_vs_bp_filter,
                object_pair_filter
            );

            physics_system->SetContactListener(&contact_listener);
        }

        ~PhysicsWorldImpl()
        {
            Clear();
        }

        void Clear()
        {
            JPH::BodyInterface& body_interface = physics_system->GetBodyInterface();
            for (auto& [entity, body_id] : entity_to_body)
            {
                body_interface.RemoveBody(body_id);
                body_interface.DestroyBody(body_id);
            }
            entity_to_body.clear();
            body_to_entity.clear();
            active_trigger_pairs.clear();
            trigger_events.clear();
            accumulator = 0.0f;
        }

    };

    PhysicsWorld::PhysicsWorld(const PhysicsWorldDesc& desc)
    {
        impl = std::make_unique<PhysicsWorldImpl>(desc);
    }

    PhysicsWorld::~PhysicsWorld() = default;

    void PhysicsWorld::Clear()
    {
        impl->Clear();
    }

    void PhysicsWorld::Step(float delta_time)
    {
        impl->accumulator += delta_time;

        int steps_taken = 0;
		// note: we use fixed timestep for stable simulation result
        while (impl->accumulator >= impl->fixed_step && steps_taken < impl->max_steps_per_frame)
        {
			// so do not pass variable delta_time
            impl->physics_system->Update(impl->fixed_step, 1, impl->temp_allocator.get(), impl->job_system.get());
            impl->accumulator -= impl->fixed_step;
            ++steps_taken;
        }

        // Generate trigger events
        Vector<ActiveTriggerPair> current_pairs;
        {
            std::lock_guard<std::mutex> lock(impl->contact_listener.mutex);
            current_pairs.reserve(impl->contact_listener.active_pairs.size());
            for (const auto& pair : impl->contact_listener.active_pairs)
            {
                auto it_a = impl->body_to_entity.find(pair.a);
                auto it_b = impl->body_to_entity.find(pair.b);
                if (it_a != impl->body_to_entity.end() && it_b != impl->body_to_entity.end())
                {
                    ActiveTriggerPair key;
                    won::ecs::Entity a = it_a->second;
                    won::ecs::Entity b = it_b->second;
                    if (a < b)
                    {
                        key.a = a;
                        key.b = b;
                    }
                    else
                    {
                        key.a = b;
                        key.b = a;
                    }
                    current_pairs.push_back(key);
                }
            }
        }

        impl->trigger_events.clear();

        auto pair_less = [](const ActiveTriggerPair& l, const ActiveTriggerPair& r)
        {
            return l.a < r.a || (l.a == r.a && l.b < r.b);
        };
        auto pair_equal = [](const ActiveTriggerPair& l, const ActiveTriggerPair& r)
        {
            return l.a == r.a && l.b == r.b;
        };

        std::sort(current_pairs.begin(), current_pairs.end(), pair_less);
        // active_trigger_pairs is kept sorted from the previous frame

        for (const ActiveTriggerPair& pair : current_pairs)
        {
            const bool was_active = std::binary_search(impl->active_trigger_pairs.begin(), impl->active_trigger_pairs.end(), pair, pair_less);
            const Collider3DTriggerEventType type = was_active ? Collider3DTriggerEventType::Stay : Collider3DTriggerEventType::Enter;
            impl->trigger_events.push_back({ type, pair.a, pair.b });
            impl->trigger_events.push_back({ type, pair.b, pair.a });
        }

        for (const ActiveTriggerPair& pair : impl->active_trigger_pairs)
        {
            if (!std::binary_search(current_pairs.begin(), current_pairs.end(), pair, pair_less))
            {
                impl->trigger_events.push_back({ Collider3DTriggerEventType::Exit, pair.a, pair.b });
                impl->trigger_events.push_back({ Collider3DTriggerEventType::Exit, pair.b, pair.a });
            }
        }

        impl->active_trigger_pairs = std::move(current_pairs);
    }

    void PhysicsWorld::AddBody(won::ecs::Entity entity, const won::ecs::TransformComponent& transform, won::ecs::Collider3DComponent& collider, won::ecs::Rigidbody3DComponent* rb, uint32_t collision_layer)
    {
        XMMATRIX world = transform.GetWorldTransform();
        XMVECTOR world_scale_vec, world_rot_vec, world_pos_vec;
        XMMatrixDecompose(&world_scale_vec, &world_rot_vec, &world_pos_vec, world);
        XMFLOAT3 world_scale; XMStoreFloat3(&world_scale, world_scale_vec);

        JPH::Ref<JPH::Shape> shape;
        if (collider.shape_type == won::ecs::Collider3DComponent::ShapeType::Sphere)
        {
            float max_scale = (std::max)((std::max)(world_scale.x, world_scale.y), world_scale.z);
            float radius = (std::max)(0.001f, collider.radius * max_scale);
            shape = new JPH::SphereShape(radius);
        }
        else
        {
            float3 extent = {
                (std::max)(0.001f, collider.half_extent.x * world_scale.x),
                (std::max)(0.001f, collider.half_extent.y * world_scale.y),
                (std::max)(0.001f, collider.half_extent.z * world_scale.z)
            };
            shape = new JPH::BoxShape(JPH::Vec3(extent.x, extent.y, extent.z));
        }

        JPH::EMotionType motion_type = JPH::EMotionType::Static;
        if (rb)
        {
            if (rb->motion_type == won::ecs::Rigidbody3DComponent::MotionType::Kinematic)
                motion_type = JPH::EMotionType::Kinematic;
            else if (rb->motion_type == won::ecs::Rigidbody3DComponent::MotionType::Dynamic)
                motion_type = JPH::EMotionType::Dynamic;
        }

        JPH::ObjectLayer object_layer = Detail::ObjectLayers::NON_MOVING;
        if (collider.IsTrigger())
        {
            object_layer = Detail::ObjectLayers::TRIGGER;
        }
        else if (motion_type == JPH::EMotionType::Dynamic || motion_type == JPH::EMotionType::Kinematic)
        {
            object_layer = Detail::ObjectLayers::MOVING;
        }

        XMVECTOR world_pos = XMVector3TransformCoord(XMLoadFloat3(&collider.offset), world);
        XMFLOAT3 pos; XMStoreFloat3(&pos, world_pos);
        XMFLOAT4 rot; XMStoreFloat4(&rot, world_rot_vec);

        JPH::BodyCreationSettings settings(
            shape,
            JPH::RVec3(pos.x, pos.y, pos.z),
            JPH::Quat(rot.x, rot.y, rot.z, rot.w),
            motion_type,
            object_layer
        );

        settings.mFriction = collider.friction;
        settings.mRestitution = collider.restitution;
        settings.mUserData = static_cast<JPH::uint64>(collision_layer);

        if (rb)
        {
            settings.mGravityFactor = rb->gravity_factor;
            settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            settings.mMassPropertiesOverride.mMass = (std::max)(0.001f, rb->mass);
            if ((rb->flags & won::ecs::Rigidbody3DComponent::LockRotation) != 0)
            {
                settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX | JPH::EAllowedDOFs::TranslationY | JPH::EAllowedDOFs::TranslationZ | JPH::EAllowedDOFs::RotationY;
            }
        }

        JPH::BodyInterface& body_interface = impl->physics_system->GetBodyInterface();
        JPH::BodyID body_id = body_interface.CreateAndAddBody(settings, JPH::EActivation::Activate);

        if (collider.IsTrigger())
        {
            body_interface.SetIsSensor(body_id, true);
        }

        if (rb && (motion_type == JPH::EMotionType::Dynamic || motion_type == JPH::EMotionType::Kinematic))
        {
            body_interface.SetLinearVelocity(body_id, JPH::Vec3(rb->linear_velocity.x, rb->linear_velocity.y, rb->linear_velocity.z));
            body_interface.SetAngularVelocity(body_id, JPH::Vec3(rb->angular_velocity.x, rb->angular_velocity.y, rb->angular_velocity.z));
        }


        if (rb)
        {
            rb->SetDirty(false);
        }
        collider.SetDirty(false);

        {
            std::unique_lock lock(impl->bodies_mutex);
            impl->entity_to_body[entity] = body_id;
            impl->body_to_entity[body_id] = entity;
        }
    }

    void PhysicsWorld::RemoveBody(won::ecs::Entity entity)
    {
        JPH::BodyID body_id;
        {
            std::unique_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
            impl->body_to_entity.erase(body_id);
            impl->entity_to_body.erase(it);
        }
        JPH::BodyInterface& body_interface = impl->physics_system->GetBodyInterface();
        body_interface.RemoveBody(body_id);
        body_interface.DestroyBody(body_id);
    }

    bool PhysicsWorld::HasBody(won::ecs::Entity entity) const
    {
        std::shared_lock lock(impl->bodies_mutex);
        return impl->entity_to_body.find(entity) != impl->entity_to_body.end();
    }

    bool PhysicsWorld::IsDynamic(won::ecs::Entity entity) const
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return false;
            body_id = it->second;
        }
        return impl->physics_system->GetBodyInterface().GetMotionType(body_id) == JPH::EMotionType::Dynamic;
    }

    void PhysicsWorld::SyncTransformToPhysics(won::ecs::Entity entity, const won::ecs::TransformComponent& transform, const won::ecs::Collider3DComponent& collider)
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
        }

        JPH::BodyInterface& body_interface = impl->physics_system->GetBodyInterface();

        XMMATRIX world = transform.GetWorldTransform();
        XMVECTOR world_scale_vec, world_rot_vec, world_pos_vec;
        XMMatrixDecompose(&world_scale_vec, &world_rot_vec, &world_pos_vec, world);

        XMVECTOR world_pos = XMVector3TransformCoord(XMLoadFloat3(&collider.offset), world);
        XMFLOAT3 pos; XMStoreFloat3(&pos, world_pos);
        XMFLOAT4 rot; XMStoreFloat4(&rot, world_rot_vec);

        JPH::RVec3 current_pos;
        JPH::Quat current_rot;
        body_interface.GetPositionAndRotation(body_id, current_pos, current_rot);

        float pos_diff = std::abs(current_pos.GetX() - pos.x) + std::abs(current_pos.GetY() - pos.y) + std::abs(current_pos.GetZ() - pos.z);
        float rot_diff = std::abs(current_rot.GetX() - rot.x) + std::abs(current_rot.GetY() - rot.y) + std::abs(current_rot.GetZ() - rot.z) + std::abs(current_rot.GetW() - rot.w);

        if (pos_diff > 1e-5f || rot_diff > 1e-5f)
        {
            body_interface.SetPositionAndRotation(body_id, JPH::RVec3(pos.x, pos.y, pos.z), JPH::Quat(rot.x, rot.y, rot.z, rot.w), JPH::EActivation::Activate);
        }
    }

    void PhysicsWorld::GetBodyTransform(won::ecs::Entity entity, float3& out_position, float4& out_rotation) const
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
        }
        JPH::RVec3 pos;
        JPH::Quat rot;
        impl->physics_system->GetBodyInterface().GetPositionAndRotation(body_id, pos, rot);
        out_position = { pos.GetX(), pos.GetY(), pos.GetZ() };
        out_rotation = { rot.GetX(), rot.GetY(), rot.GetZ(), rot.GetW() };
    }

    void PhysicsWorld::GetBodyVelocity(won::ecs::Entity entity, float3& out_linear, float3& out_angular) const
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
        }
        JPH::Vec3 lin = impl->physics_system->GetBodyInterface().GetLinearVelocity(body_id);
        JPH::Vec3 ang = impl->physics_system->GetBodyInterface().GetAngularVelocity(body_id);
        out_linear = { lin.GetX(), lin.GetY(), lin.GetZ() };
        out_angular = { ang.GetX(), ang.GetY(), ang.GetZ() };
    }

    void PhysicsWorld::SetBodyCollisionLayer(won::ecs::Entity entity, uint32_t collision_layer)
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
        }
        impl->physics_system->GetBodyInterface().SetUserData(body_id, static_cast<JPH::uint64>(collision_layer));
    }

    void PhysicsWorld::AddForce(won::ecs::Entity entity, const float3& force)
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
        }
        impl->physics_system->GetBodyInterface().AddForce(body_id, JPH::Vec3(force.x, force.y, force.z));
    }

    void PhysicsWorld::AddImpulse(won::ecs::Entity entity, const float3& impulse)
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
        }
        impl->physics_system->GetBodyInterface().AddImpulse(body_id, JPH::Vec3(impulse.x, impulse.y, impulse.z));
    }

    void PhysicsWorld::AddTorque(won::ecs::Entity entity, const float3& torque)
    {
        JPH::BodyID body_id;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->entity_to_body.find(entity);
            if (it == impl->entity_to_body.end())
                return;
            body_id = it->second;
        }
        impl->physics_system->GetBodyInterface().AddTorque(body_id, JPH::Vec3(torque.x, torque.y, torque.z));
    }

    const Vector<Collider3DTriggerEvent>& PhysicsWorld::GetTriggerEvents() const
    {
        return impl->trigger_events;
    }
}
