#pragma once
#include "Types.h"
#include "MathUtils.h"
#include "ShaderInterop_Renderer.h"

namespace won::ecs
{
    struct CameraComponent
    {
        enum FLAGS
        {
            EMPTY = 0,
            DIRTY = 1 << 0,
            IS_ORTHOGRAPHIC = 1 << 1,
        };

        uint32 flags = EMPTY;

        // these values will be updated on CameraUpdateSystem
        // you can use TransformComponent for manipulation !!
        float3 eye = { 0, 0, 0 }; // vec(0, 0, 0) * transform_matrix
        float3 forward = { 0, 1, 0 }; // vec(0, 1, 0) * transform_matrix
        float3 up = { 0, 0, 1 }; // vec(0, 0, 1) * transform_matrix
        float4x4 view, projection, view_projection;
        float4x4 inv_view, inv_projection, inv_view_projection;

        float near = 0.1f;
        float far = 1000.f;
        float aspect_ratio = 16.f / 9.f; // width / height

        // perspective
        float fov_y = math::PI / 3.0f;

        //orthographic
        float ortho_vertical_size = 100.f;

        float aperture = 4.0; // f-number e.g. 16 for small aperture, 1.4 for wide aperture ...
        float shutter_speed = 1 / 125.f; // in seconds
        float sensitivity = 100; // ISO

        constexpr void SetDirty(bool value = true) { if (value) { flags |= DIRTY; } else { flags &= ~DIRTY; } }
        constexpr bool IsDirty() const { return flags & DIRTY; }

        constexpr void SetOrtho(bool value = true) { if (value) { flags |= IS_ORTHOGRAPHIC; } else { flags &= ~IS_ORTHOGRAPHIC; } SetDirty(); }
        constexpr bool IsOrtho() const { return flags & IS_ORTHOGRAPHIC; }

        constexpr void SetNearFar(float near_value, float far_value) { near = near_value; far = far_value; SetDirty(); }
        constexpr void SetAspectRatio(float value) { aspect_ratio = value; SetDirty(); }
        constexpr void SetFOV_Y(float value) { fov_y = value; SetDirty(); }
        constexpr void SetOrthoVerticalSize(float value) { ortho_vertical_size = value; SetDirty(); }

        constexpr void SetApertureSize(float value) { aperture = value; }
        constexpr void SetShutterSpeed(float value) { shutter_speed = value; }
        constexpr void SetSensitivity(float value) { sensitivity = value; }
    };
}
