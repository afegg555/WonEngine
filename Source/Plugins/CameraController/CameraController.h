#pragma once
#include "IPlugin.h"
#include "MathUtils.h"

inline constexpr const char* WON_IID_CAMERA_CONTROLLER = "CameraController";
inline constexpr const char* WON_VID_CAMERA_CONTROLLER = "0.1.0";

namespace won::plugin
{
    struct CameraState
    {
        // in-out 
        XMFLOAT3 cam_pos{ 0.f, 0.f, 0.f };
        XMFLOAT3 cam_view{ 0.f, 0.f, -1.f };
        XMFLOAT3 cam_up{ 0.f, 1.f, 0.f };
    };

    struct ControllerState
    {
        float zoom_sensitivity{ 0.005f };
        float rotate_speed{ 1.f };
        //XMFLOAT3 rotate_pivot{ 0.f, 0.f, 0.f };
        XMFLOAT2 screen_size{ 800.f, 600.f };
    };

    // public APIs
    struct CameraControllerAPI
    {
        void (*SetControllerState)(IPlugin* self, const ControllerState& controller_state);

        void (*Start)(IPlugin* self, const XMFLOAT2& mouse_pos, const CameraState& cam_state_in);
        void (*PanMove)(IPlugin* self, const XMFLOAT2& mouse_pos, CameraState& cam_state_out);
        void (*Rotate)(IPlugin* self, const XMFLOAT2& mouse_pos, CameraState& cam_state_out);
    };
}