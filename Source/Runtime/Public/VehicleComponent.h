#pragma once
#include "Types.h"
#include "MathUtils.h"
#include "Entity.h"

namespace won::ecs
{
    // !! lengths are body local units, entity transform scale is ignored
    // !! keep the vehicle entity at scale 1
    struct VehicleWheel
    {
        // ===== car body =====
        //       |  <- attachment_position (spring top)
        //       |
        //       |  <- suspension_length
        //       |
        //      (O) <- wheel (radius)
        //  ---------------- ground
        float3 attachment_position = { 0.0f, 0.0f, 0.0f }; // body local

        float radius = 0.35f; // wheel radius
        float width = 0.2f; // wheel width, ground contact test only
        float suspension_min_length = 0.3f; // fully compressed spring length
        float suspension_max_length = 0.5f; // spring rest length
        float suspension_frequency = 1.5f; // spring stiffness in Hz (offroad 0.8..1 sedan 1..1.5, sports 2)
        float suspension_damping = 0.5f; // 1 = no oscillation
        float max_steer_angle_radians = math::PI / 6.0f; // steering limit in radians
        float max_brake_torque = 1500.0f; // foot brake torque in Nm
        float max_hand_brake_torque = 0.0f; // can differentiate rear-wheel-only brake !
        bool driven = false; // can differentiate FWD, RWD, 4WD .. !
        Entity visual_entity = INVALID_ENTITY; // entity that follows this wheel

        // These values are updated by PhysicsUpdateSystem.
        float3 world_position = { 0.0f, 0.0f, 0.0f };
        float4 world_rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        float suspension_length = 0.0f;
        bool has_contact = false; // wheel is on the ground
    };

    struct VehicleComponent
    {
        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            Enabled = 1 << 1,
            AutomaticTransmission = 1 << 2,
        };

        uint32 flags = Dirty | Enabled | AutomaticTransmission;

        Vector<VehicleWheel> wheels = {
            { { -0.9f, -0.2f, 1.4f }, 0.35f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, math::PI / 6.0f, 1500.0f, 0.0f, true },
            { { 0.9f, -0.2f, 1.4f }, 0.35f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, math::PI / 6.0f, 1500.0f, 0.0f, true },
            { { -0.9f, -0.2f, -1.4f }, 0.35f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, 0.0f, 1500.0f, 4000.0f, true },
            { { 0.9f, -0.2f, -1.4f }, 0.35f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, 0.0f, 1500.0f, 4000.0f, true },
        };

        float3 up = { 0.0f, 1.0f, 0.0f }; // body local up, suspension pushes along -up
        float3 forward = { 0.0f, 0.0f, 1.0f }; // body local forward
        float max_pitch_roll_angle_radians = math::PI / 3.0f; // max angle between body up and world up, pi = no limit

        float engine_max_torque = 500.0f; // peak engine torque in Nm
        float engine_min_rpm = 1000.0f; // idle rpm, engine never drops below this
        float engine_max_rpm = 6000.0f; // redline rpm, gearbox shifts to stay inside min..max
        float differential_ratio = 3.42f; // final drive ratio applied after the selected transmission gear
        Vector<float> forward_gear_ratios = { 2.66f, 1.78f, 1.3f, 1.0f, 0.74f };
        Vector<float> reverse_gear_ratios = { -2.9f };
        float shift_up_rpm = 4000.0f;
        float shift_down_rpm = 2000.0f;
        float shift_duration = 0.5f;

        float throttle_input = 0.0f; // -1..1, negative = reverse
        float steer_input = 0.0f; // -1..1, positive = right
        float brake_input = 0.0f; // 0..1
        float hand_brake_input = 0.0f; // 0..1

        // These values are updated by PhysicsUpdateSystem.
        float speed = 0.0f; // forward speed in m/s
        float engine_rpm = 0.0f; // engine rpm
        int current_gear = 0;
        bool shifting_gear = false;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return (flags & Dirty) != 0; }
        constexpr void SetEnabled(bool value = true) { if (IsEnabled() == value) { return; } if (value) { flags |= Enabled; } else { flags &= ~Enabled; } SetDirty(); }
        constexpr bool IsEnabled() const { return (flags & Enabled) != 0; }
        constexpr void SetAutomaticTransmission(bool value = true) { if (IsAutomaticTransmission() == value) { return; } if (value) { flags |= AutomaticTransmission; } else { flags &= ~AutomaticTransmission; } SetDirty(); }
        constexpr bool IsAutomaticTransmission() const { return (flags & AutomaticTransmission) != 0; }
    };

    enum class VehiclePreset
    {
        Sedan = 0,
        Sports,
        Offroad,
    };

    // authoring starting point, overwrites every authored field of the component
    inline VehicleComponent MakeVehiclePreset(VehiclePreset preset)
    {
        VehicleComponent vehicle = {};
        switch (preset)
        {
        case VehiclePreset::Sedan:
        default:
            vehicle.wheels = {
                { { -0.8f, -0.2f, 1.35f }, 0.33f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, math::PI / 6.0f, 1500.0f, 0.0f, true },
                { { 0.8f, -0.2f, 1.35f }, 0.33f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, math::PI / 6.0f, 1500.0f, 0.0f, true },
                { { -0.8f, -0.2f, -1.35f }, 0.33f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, 0.0f, 1500.0f, 4000.0f, false },
                { { 0.8f, -0.2f, -1.35f }, 0.33f, 0.2f, 0.3f, 0.5f, 1.5f, 0.5f, 0.0f, 1500.0f, 4000.0f, false },
            };
            vehicle.engine_max_torque = 350.0f;
            vehicle.engine_min_rpm = 800.0f;
            vehicle.engine_max_rpm = 6500.0f;
            vehicle.differential_ratio = 3.42f;
            break;

        case VehiclePreset::Sports:
            vehicle.wheels = {
                { { -0.85f, -0.28f, 1.3f }, 0.34f, 0.26f, 0.22f, 0.38f, 2.2f, 0.55f, math::PI / 6.0f, 1800.0f, 0.0f, false },
                { { 0.85f, -0.28f, 1.3f }, 0.34f, 0.26f, 0.22f, 0.38f, 2.2f, 0.55f, math::PI / 6.0f, 1800.0f, 0.0f, false },
                { { -0.85f, -0.28f, -1.3f }, 0.34f, 0.26f, 0.22f, 0.38f, 2.2f, 0.55f, 0.0f, 1800.0f, 4000.0f, true },
                { { 0.85f, -0.28f, -1.3f }, 0.34f, 0.26f, 0.22f, 0.38f, 2.2f, 0.55f, 0.0f, 1800.0f, 4000.0f, true },
            };
            vehicle.engine_max_torque = 500.0f;
            vehicle.engine_min_rpm = 1000.0f;
            vehicle.engine_max_rpm = 8000.0f;
            vehicle.differential_ratio = 3.9f;
            break;

        case VehiclePreset::Offroad:
            vehicle.wheels = {
                { { -0.95f, -0.15f, 1.5f }, 0.45f, 0.3f, 0.35f, 0.75f, 1.0f, 0.45f, math::PI / 6.0f, 2000.0f, 0.0f, true },
                { { 0.95f, -0.15f, 1.5f }, 0.45f, 0.3f, 0.35f, 0.75f, 1.0f, 0.45f, math::PI / 6.0f, 2000.0f, 0.0f, true },
                { { -0.95f, -0.15f, -1.5f }, 0.45f, 0.3f, 0.35f, 0.75f, 1.0f, 0.45f, 0.0f, 2000.0f, 4000.0f, true },
                { { 0.95f, -0.15f, -1.5f }, 0.45f, 0.3f, 0.35f, 0.75f, 1.0f, 0.45f, 0.0f, 2000.0f, 4000.0f, true },
            };
            vehicle.engine_max_torque = 600.0f;
            vehicle.engine_min_rpm = 800.0f;
            vehicle.engine_max_rpm = 5500.0f;
            vehicle.differential_ratio = 4.3f;
            break;
        }

        return vehicle;
    }
}
