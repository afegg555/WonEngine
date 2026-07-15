#pragma once
#include <cmath>
#include "Types.h"
#include "MathUtils.h"
#include "Primitives.h"
#include "ShaderInterop_Renderer.h"

namespace won::ecs
{
    struct CameraComponent
    {
        enum Flags
        {
            Empty = 0,
            Dirty = 1 << 0,
            IsOrthographic = 1 << 1,
            AutoExposure = 1 << 2,
        };

        uint32 flags = Empty;

        // These values are updated by CameraUpdateSystem.
        // Use TransformComponent for manipulation.
        float3 eye = { 0, 0, 0 }; // vec(0, 0, 0) * transform_matrix
        float3 forward = { 0, 0, 1 }; // vec(0, 0, 1) * transform_matrix
        float3 up = { 0, 1, 0 }; // vec(0, 1, 0) * transform_matrix
        float4x4 view, projection, view_projection;
        float4x4 inv_view, inv_projection, inv_view_projection;
        float3 corners_np[4]; // top-left, top-right, bottom-left, bottom-right
        float3 corners_fp[4];
        math::Frustum frustum;
		float exposure_multiplier = 0.0f;


        float near_plane = 0.1f;
        float far_plane = 1000.f;
        float aspect_ratio = 16.f / 9.f; // width / height

        // perspective
        float fov_y = math::PI / 3.0f;

        //orthographic
        float ortho_vertical_size = 100.f;

        float aperture = 4.0; // f-number e.g. 16 for small aperture, 1.4 for wide aperture ...
        float shutter_speed = 1 / 125.f; // in seconds
        float sensitivity = 100; // ISO

        static constexpr float auto_exposure_target = 0.18f; // metering target: 18% middle-gray key (internal)

        float exposure_compensation = 0.0f; // EV, applied in both manual and auto ("+1 = one stop brighter")
        float auto_exposure_min_ev = -6.0f; // EV100, darkest scene the auto exposure resolves to
        float auto_exposure_max_ev = 16.0f; // EV100, brightest scene the auto exposure resolves to
        float auto_exposure_speed = 2.0f;

        uint32 culling_mask = 0xFFFFFFFF;

        constexpr void SetDirty(bool value = true) { if (value) { flags |= Dirty; } else { flags &= ~Dirty; } }
        constexpr bool IsDirty() const { return flags & Dirty; }

        constexpr void SetOrtho(bool value = true) { if (value) { flags |= IsOrthographic; } else { flags &= ~IsOrthographic; } SetDirty(); }
        constexpr bool IsOrtho() const { return flags & IsOrthographic; }

        constexpr void SetAutoExposure(bool value = true) { if (value) { flags |= AutoExposure; } else { flags &= ~AutoExposure; } }
        constexpr bool IsAutoExposure() const { return flags & AutoExposure; }

        constexpr void SetNearFar(float near_value, float far_value) { near_plane = near_value; far_plane = far_value; SetDirty(); }
        constexpr void SetAspectRatio(float value) { aspect_ratio = value; SetDirty(); }
        constexpr void SetFOV_Y(float value) { fov_y = value; SetDirty(); }
        constexpr void SetOrthoVerticalSize(float value) { ortho_vertical_size = value; SetDirty(); }

        constexpr void SetApertureSize(float value) { aperture = value; }
        constexpr void SetShutterSpeed(float value) { shutter_speed = value; }
        constexpr void SetSensitivity(float value) { sensitivity = value; }

        constexpr float GetPhysicalExposure() const { return (shutter_speed * sensitivity) / (1.2f * aperture * aperture * 100.0f); }

        static float ExposureFromEV100(float ev100) { return 1.0f / (1.2f * std::exp2(ev100)); }
    };
}
