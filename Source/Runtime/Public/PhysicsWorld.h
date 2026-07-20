#pragma once
#include "Types.h"
#include "RuntimeExport.h"

namespace won::ecs
{
    struct TransformComponent;
    struct Collider3DComponent;
    struct Rigidbody3DComponent;
    using Entity = uint64;
}

namespace won::physics
{
    struct PhysicsWorldImpl;

    inline constexpr uint32_t default_temp_allocator_size = 32 * 1024 * 1024;
    inline constexpr uint32_t default_max_bodies = 10240;
    inline constexpr uint32_t default_max_body_pairs = 65536;
    inline constexpr uint32_t default_max_contact_constraints = 20480;
    inline constexpr float    default_physics_hz = 60.0f;
    inline constexpr int      default_max_steps_per_frame = 5;

    struct PhysicsWorldDesc
    {
        uint32_t temp_allocator_size    = default_temp_allocator_size;
        uint32_t max_bodies             = default_max_bodies;
        uint32_t max_body_pairs         = default_max_body_pairs;
        uint32_t max_contact_constraints = default_max_contact_constraints;
        float    physics_hz             = default_physics_hz;
        int      max_steps_per_frame    = default_max_steps_per_frame;
    };

    enum class Collider3DTriggerEventType
    {
        Enter,
        Stay,
        Exit,
    };

    struct Collider3DTriggerEvent
    {
        Collider3DTriggerEventType type = Collider3DTriggerEventType::Enter;
        won::ecs::Entity self = 0;
        won::ecs::Entity other = 0;
    };

    struct RayCastHit
    {
        won::ecs::Entity entity = 0;
        float distance = 0.0f;
        float3 point = { 0.0f, 0.0f, 0.0f };
        float3 normal = { 0.0f, 0.0f, 0.0f };
    };

    class WONENGINE_API PhysicsWorld
    {
    public:
        PhysicsWorld(const PhysicsWorldDesc& desc = {});
        ~PhysicsWorld();

        void Step(float delta_time);
        void Clear();

        struct HeightFieldShapeDesc
        {
            const float* samples = nullptr;
            uint32_t samples_x = 0;
            uint32_t samples_z = 0;
            float cell_x = 0.0f;
            float cell_z = 0.0f;
            float offset_x = 0.0f;
            float offset_z = 0.0f;
        };

        void AddBody(won::ecs::Entity entity, const won::ecs::TransformComponent& transform, won::ecs::Collider3DComponent& collider, won::ecs::Rigidbody3DComponent* rb, uint32_t collision_layer = 0, const HeightFieldShapeDesc* height_field = nullptr);
        void RemoveBody(won::ecs::Entity entity);
        bool HasBody(won::ecs::Entity entity) const;

        bool IsDynamic(won::ecs::Entity entity) const;

        void SyncTransformToPhysics(won::ecs::Entity entity, const won::ecs::TransformComponent& transform, const won::ecs::Collider3DComponent& collider);
        
        void GetBodyTransform(won::ecs::Entity entity, float3& out_position, float4& out_rotation) const;
        void GetBodyVelocity(won::ecs::Entity entity, float3& out_linear, float3& out_angular) const;

        void SetBodyCollisionLayer(won::ecs::Entity entity, uint32_t collision_layer);

        void AddForce(won::ecs::Entity entity, const float3& force);
        void AddImpulse(won::ecs::Entity entity, const float3& impulse);
        void AddTorque(won::ecs::Entity entity, const float3& torque);

        bool RayCast(const float3& origin, const float3& direction, float max_distance, RayCastHit& out_hit, uint32_t layer_mask = 0xFFFFFFFFu) const;
        bool SphereCast(const float3& origin, const float3& direction, float radius, float max_distance, RayCastHit& out_hit, uint32_t layer_mask = 0xFFFFFFFFu) const;
        void OverlapSphere(const float3& center, float radius, Vector<won::ecs::Entity>& out_entities, uint32_t layer_mask = 0xFFFFFFFFu) const;

        const Vector<Collider3DTriggerEvent>& GetTriggerEvents() const;

    private:
        std::unique_ptr<PhysicsWorldImpl> impl;
    };

    WONENGINE_API void Initialize();
    WONENGINE_API void Shutdown();

    WONENGINE_API void ResetCollisionMatrix();
    WONENGINE_API void SetLayerCollision(uint32_t layer_a, uint32_t layer_b, bool enabled);
    WONENGINE_API bool GetLayerCollision(uint32_t layer_a, uint32_t layer_b);
}
