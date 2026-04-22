#pragma once
#include "IPlugin.h"
#include "MathUtils.h"

inline constexpr const char* WON_IID_CAMERA_CONTROLLER = "CameraController";
inline constexpr const char* WON_VID_CAMERA_CONTROLLER = "0.2.0";

namespace won::plugin
{
    enum class CameraInteractionMode : uint32
    {
        None,
        PanMove,
        Rotate,
        Orbit,
    };

    struct CameraState
    {
        // in-out
        float3 cam_pos{ 0.f, 0.f, 0.f };
        float4 cam_rotation{ 0.f, 0.f, 0.f, 1.f };
    };

    struct ControllerState
    {
        float zoom_speed{ 0.005f };
        float rotate_speed{ 1.f };
        float orbit_speed{ 1.f };
        float3 focus_point{ 0.f, 0.f, 0.f };
        float2 screen_size{ 800.f, 600.f };
    };

    // public APIs
    struct CameraControllerAPI
    {
        void (*SetControllerState)(IPlugin* self, const ControllerState& controller_state);

        void (*BeginInteraction)(IPlugin* self, CameraInteractionMode mode, const CameraState& cam_state);
        void (*UpdateInteraction)(IPlugin* self, const float2& mouse_delta, CameraState& cam_state);
        void (*EndInteraction)(IPlugin* self);
    };
}
