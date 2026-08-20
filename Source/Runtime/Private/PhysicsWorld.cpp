#include "PhysicsWorld.h"
#include "Backlog.h"
#include "Primitives.h"
#include "TransformComponent.h"
#include "Collider3DComponent.h"
#include "Rigidbody3DComponent.h"
#include "JointComponent.h"
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
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyInterface.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Collision/ContactListener.h>
#include <Jolt/Physics/Collision/NarrowPhaseQuery.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyLockInterface.h>
#include <Jolt/Physics/Constraints/Constraint.h>
#include <Jolt/Physics/Constraints/TwoBodyConstraint.h>
#include <Jolt/Physics/Constraints/FixedConstraint.h>
#include <Jolt/Physics/Constraints/HingeConstraint.h>

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

		class BPLayerInterfaceImpl final : public JPH::BroadPhaseLayerInterface // object layer -> broad phase layer mapping
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

		class ObjectVsBroadPhaseLayerFilterImpl : public JPH::ObjectVsBroadPhaseLayerFilter // object layer -> broad phase layer filter (should this object layer interact with that broad phase layer?)
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

		class ObjectLayerPairFilterImpl : public JPH::ObjectLayerPairFilter // object layer -> object layer filter (should this object layer interact with that object layer?)
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
		class MyContactListener : public JPH::ContactListener // contact listener for trigger events
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

        struct JointRecord
        {
            JPH::Ref<JPH::Constraint> constraint;
            JPH::BodyID body_a;
            JPH::BodyID body_b;
        };
        mutable std::mutex joints_mutex;
        UnorderedMap<won::ecs::Entity, JointRecord> joints;

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
            for (auto& [entity, record] : joints)
            {
                if (record.constraint != nullptr)
                {
                    physics_system->RemoveConstraint(record.constraint);
                }
            }
            joints.clear();

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

    void PhysicsWorld::AddBody(won::ecs::Entity entity, const won::ecs::TransformComponent& transform, won::ecs::Collider3DComponent& collider, won::ecs::Rigidbody3DComponent* rb, uint32_t collision_layer, const HeightFieldShapeDesc* height_field)
    {
        XMMATRIX world = transform.GetWorldTransform();
        XMVECTOR world_scale_vec, world_rot_vec, world_pos_vec;
        XMMatrixDecompose(&world_scale_vec, &world_rot_vec, &world_pos_vec, world);
        XMFLOAT3 world_scale; XMStoreFloat3(&world_scale, world_scale_vec);

        JPH::Ref<JPH::Shape> shape;
        if (collider.shape_type == won::ecs::Collider3DComponent::ShapeType::HeightField)
        {
            if (!height_field || !height_field->samples || height_field->samples_x < 2 || height_field->samples_z < 2)
            {
                wonlog_warning("[PhysicsWorld] HeightField collider on entity %llu has no height field data, body skipped", static_cast<unsigned long long>(entity));
                return;
            }
            if (rb && rb->motion_type != won::ecs::Rigidbody3DComponent::MotionType::Static)
            {
                wonlog_warning("[PhysicsWorld] HeightField collider is static-only, entity %llu rigidbody motion ignored", static_cast<unsigned long long>(entity));
            }

            constexpr uint32_t block_size = 2;
            const uint32_t max_samples = (std::max)(height_field->samples_x, height_field->samples_z);
            const uint32_t sample_count = ((max_samples + block_size - 1) / block_size) * block_size;

            Vector<float> padded(static_cast<Size>(sample_count) * sample_count, JPH::HeightFieldShapeConstants::cNoCollisionValue);
            for (uint32_t j = 0; j < height_field->samples_z; ++j)
            {
                for (uint32_t i = 0; i < height_field->samples_x; ++i)
                {
                    padded[j * sample_count + i] = height_field->samples[j * height_field->samples_x + i];
                }
            }

            const JPH::Vec3 shape_offset(height_field->offset_x * world_scale.x, 0.0f, height_field->offset_z * world_scale.z);
            const JPH::Vec3 shape_scale(height_field->cell_x * world_scale.x, world_scale.y, height_field->cell_z * world_scale.z);
            JPH::HeightFieldShapeSettings settings(padded.data(), shape_offset, shape_scale, sample_count);
            settings.mBlockSize = block_size;
            JPH::ShapeSettings::ShapeResult result = settings.Create();
            if (result.HasError())
            {
                wonlog_warning("[PhysicsWorld] HeightField shape creation failed for entity %llu: %s", static_cast<unsigned long long>(entity), result.GetError().c_str());
                return;
            }
            shape = result.Get();
        }
        else if (collider.shape_type == won::ecs::Collider3DComponent::ShapeType::Sphere)
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
        if (rb && collider.shape_type != won::ecs::Collider3DComponent::ShapeType::HeightField)
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

        {
            std::lock_guard lock(impl->joints_mutex);
            for (auto it = impl->joints.begin(); it != impl->joints.end(); )
            {
                if (it->second.body_a == body_id || it->second.body_b == body_id)
                {
                    if (it->second.constraint != nullptr)
                    {
                        impl->physics_system->RemoveConstraint(it->second.constraint);
                    }
                    it = impl->joints.erase(it);
                }
                else
                {
                    ++it;
                }
            }
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

    namespace
    {
		class LayerMaskBodyFilter : public JPH::BodyFilter // temp filter for raycast and spherecast to filter by layer mask
        {
        public:
            explicit LayerMaskBodyFilter(uint32_t mask) : layer_mask(mask) {}

            bool ShouldCollideLocked(const JPH::Body& body) const override
            {
                const uint32_t layer = static_cast<uint32_t>(body.GetUserData()) & 31u;
                return (layer_mask & (1u << layer)) != 0u;
            }

        private:
            uint32_t layer_mask;
        };
    }

    float PhysicsWorld::GetFixedStepSeconds() const
    {
        return impl->fixed_step;
    }

    int PhysicsWorld::GetMaxStepsPerFrame() const
    {
        return impl->max_steps_per_frame;
    }

    bool PhysicsWorld::RayCast(const float3& origin, const float3& direction, float max_distance, RayCastHit& out_hit, uint32_t layer_mask) const
    {
        out_hit = {};

        const float direction_length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (direction_length <= 1e-6f || max_distance <= 0.0f)
        {
            return false;
        }
        const float inv_length = 1.0f / direction_length;
        const JPH::Vec3 scaled_direction(
            direction.x * inv_length * max_distance,
            direction.y * inv_length * max_distance,
            direction.z * inv_length * max_distance);

        const JPH::RRayCast ray(JPH::RVec3(origin.x, origin.y, origin.z), scaled_direction);
        JPH::RayCastResult result;

        const LayerMaskBodyFilter body_filter(layer_mask);
        if (!impl->physics_system->GetNarrowPhaseQuery().CastRay(ray, result, {}, {}, body_filter))
        {
            return false;
        }

        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->body_to_entity.find(result.mBodyID);
            if (it == impl->body_to_entity.end())
            {
                return false;
            }
            out_hit.entity = it->second;
        }

        const JPH::RVec3 hit_point = ray.GetPointOnRay(result.mFraction);
        JPH::Vec3 hit_normal = JPH::Vec3::sZero();
        {
            JPH::BodyLockRead lock(impl->physics_system->GetBodyLockInterface(), result.mBodyID);
            if (lock.Succeeded())
            {
                hit_normal = lock.GetBody().GetWorldSpaceSurfaceNormal(result.mSubShapeID2, hit_point);
            }
        }

        out_hit.distance = result.mFraction * max_distance;
        out_hit.point = { (float)hit_point.GetX(), (float)hit_point.GetY(), (float)hit_point.GetZ() };
        out_hit.normal = { hit_normal.GetX(), hit_normal.GetY(), hit_normal.GetZ() };
        return true;
    }

    bool PhysicsWorld::SphereCast(const float3& origin, const float3& direction, float radius, float max_distance, RayCastHit& out_hit, uint32_t layer_mask) const
    {
        out_hit = {};

        const float direction_length = std::sqrt(direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (direction_length <= 1e-6f || max_distance <= 0.0f || radius <= 0.0f)
        {
            return false;
        }
        const float inv_length = 1.0f / direction_length;
        const JPH::Vec3 scaled_direction(
            direction.x * inv_length * max_distance,
            direction.y * inv_length * max_distance,
            direction.z * inv_length * max_distance);

        JPH::SphereShape sphere_shape(radius);
        sphere_shape.SetEmbedded();

        const JPH::RShapeCast shape_cast(
            &sphere_shape,
            JPH::Vec3::sReplicate(1.0f),
            JPH::RMat44::sTranslation(JPH::RVec3(origin.x, origin.y, origin.z)),
            scaled_direction);

        JPH::ShapeCastSettings settings;
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        const LayerMaskBodyFilter body_filter(layer_mask);

        impl->physics_system->GetNarrowPhaseQuery().CastShape(
            shape_cast,
            settings,
            JPH::RVec3::sZero(),
            collector,
            {},
            {},
            body_filter);

        if (!collector.HadHit())
        {
            return false;
        }

        {
            std::shared_lock lock(impl->bodies_mutex);
            auto it = impl->body_to_entity.find(collector.mHit.mBodyID2);
            if (it == impl->body_to_entity.end())
            {
                return false;
            }
            out_hit.entity = it->second;
        }

        const JPH::Vec3 contact_point = collector.mHit.mContactPointOn2;
        JPH::Vec3 hit_normal = JPH::Vec3::sZero();
        {
            JPH::BodyLockRead lock(impl->physics_system->GetBodyLockInterface(), collector.mHit.mBodyID2);
            if (lock.Succeeded())
            {
                hit_normal = lock.GetBody().GetWorldSpaceSurfaceNormal(collector.mHit.mSubShapeID2, JPH::RVec3(contact_point));
            }
        }

        out_hit.distance = collector.mHit.mFraction * max_distance;
        out_hit.point = { contact_point.GetX(), contact_point.GetY(), contact_point.GetZ() };
        out_hit.normal = { hit_normal.GetX(), hit_normal.GetY(), hit_normal.GetZ() };
        return true;
    }

    void PhysicsWorld::OverlapSphere(const float3& center, float radius, Vector<won::ecs::Entity>& out_entities, uint32_t layer_mask) const
    {
        out_entities.clear();
        if (radius <= 0.0f)
        {
            return;
        }

        JPH::SphereShape sphere_shape(radius);
        sphere_shape.SetEmbedded();

        JPH::CollideShapeSettings settings;
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        const JPH::RMat44 transform = JPH::RMat44::sTranslation(JPH::RVec3(center.x, center.y, center.z));
        const LayerMaskBodyFilter body_filter(layer_mask);

        impl->physics_system->GetNarrowPhaseQuery().CollideShape(
            &sphere_shape,
            JPH::Vec3::sReplicate(1.0f),
            transform,
            settings,
            JPH::RVec3::sZero(),
            collector,
            {},
            {},
            body_filter);

        std::shared_lock lock(impl->bodies_mutex);
        for (const JPH::CollideShapeResult& hit : collector.mHits)
        {
            auto it = impl->body_to_entity.find(hit.mBodyID2);
            if (it == impl->body_to_entity.end())
            {
                continue;
            }
            const won::ecs::Entity entity = it->second;
            if (std::find(out_entities.begin(), out_entities.end(), entity) == out_entities.end())
            {
                out_entities.push_back(entity);
            }
        }
    }

    void PhysicsWorld::AddJoint(won::ecs::Entity owner_entity, const won::ecs::JointComponent& joint)
    {
        JPH::BodyID owner_id;
        JPH::BodyID other_id;
        bool other_valid = false;
        {
            std::shared_lock lock(impl->bodies_mutex);
            auto owner_it = impl->entity_to_body.find(owner_entity);
            if (owner_it == impl->entity_to_body.end())
            {
                return;
            }
            owner_id = owner_it->second;
            if (joint.connected_entity != 0)
            {
                auto other_it = impl->entity_to_body.find(joint.connected_entity);
                if (other_it != impl->entity_to_body.end())
                {
                    other_id = other_it->second;
                    other_valid = true;
                }
            }
        }

        const JPH::BodyLockInterface& lock_interface = impl->physics_system->GetBodyLockInterfaceNoLock();
        JPH::Body* owner_body = lock_interface.TryGetBody(owner_id);
        JPH::Body* other_body = other_valid ? lock_interface.TryGetBody(other_id) : &JPH::Body::sFixedToWorld;
        if (owner_body == nullptr || other_body == nullptr)
        {
            return;
        }

        JPH::Ref<JPH::Constraint> constraint;
        if (joint.type == won::ecs::JointComponent::JointType::Hinge)
        {
            JPH::Vec3 hinge_axis(joint.axis.x, joint.axis.y, joint.axis.z);
            if (hinge_axis.LengthSq() < 1e-8f)
            {
                hinge_axis = JPH::Vec3::sAxisY();
            }
            hinge_axis = hinge_axis.Normalized();
            const JPH::Vec3 reference = std::abs(hinge_axis.GetY()) < 0.99f ? JPH::Vec3::sAxisY() : JPH::Vec3::sAxisX();
            const JPH::Vec3 normal_axis = hinge_axis.Cross(reference).Normalized();

            JPH::HingeConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mPoint1 = settings.mPoint2 = JPH::RVec3(joint.anchor.x, joint.anchor.y, joint.anchor.z);
            settings.mHingeAxis1 = settings.mHingeAxis2 = hinge_axis;
            settings.mNormalAxis1 = settings.mNormalAxis2 = normal_axis;
            if (joint.use_limit)
            {
                settings.mLimitsMin = joint.limit_min;
                settings.mLimitsMax = joint.limit_max;
            }
            constraint = settings.Create(*other_body, *owner_body);
        }
        else
        {
            JPH::FixedConstraintSettings settings;
            settings.mSpace = JPH::EConstraintSpace::WorldSpace;
            settings.mAutoDetectPoint = true;
            constraint = settings.Create(*other_body, *owner_body);
        }

        if (constraint == nullptr)
        {
            return;
        }

        PhysicsWorldImpl::JointRecord record;
        record.constraint = constraint;
        record.body_a = owner_id;
        record.body_b = other_valid ? other_id : JPH::BodyID();

        std::lock_guard lock(impl->joints_mutex);
        auto existing = impl->joints.find(owner_entity);
        if (existing != impl->joints.end())
        {
            if (existing->second.constraint != nullptr)
            {
                impl->physics_system->RemoveConstraint(existing->second.constraint);
            }
            existing->second = record;
        }
        else
        {
            impl->joints[owner_entity] = record;
        }
        impl->physics_system->AddConstraint(constraint);
    }

    void PhysicsWorld::RemoveJoint(won::ecs::Entity owner_entity)
    {
        std::lock_guard lock(impl->joints_mutex);
        auto it = impl->joints.find(owner_entity);
        if (it == impl->joints.end())
        {
            return;
        }
        if (it->second.constraint != nullptr)
        {
            impl->physics_system->RemoveConstraint(it->second.constraint);
        }
        impl->joints.erase(it);
    }

    bool PhysicsWorld::HasJoint(won::ecs::Entity owner_entity) const
    {
        std::lock_guard lock(impl->joints_mutex);
        return impl->joints.find(owner_entity) != impl->joints.end();
    }

    const Vector<Collider3DTriggerEvent>& PhysicsWorld::GetTriggerEvents() const
    {
        return impl->trigger_events;
    }
}
