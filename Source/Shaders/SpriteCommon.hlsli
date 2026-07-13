#ifndef SPRITE_COMMON
#define SPRITE_COMMON

#include "ShaderInterop_Sprite.h"
#include "Common.hlsli"
#include "ShaderInterop_Renderer.h"

#define sampler_sprite sampler_linear_wrap
#define sampler_sprite_clamp sampler_linear_clamp

struct VertexInput
{
    uint vertex_id : SV_VertexID;
    uint instance_id : SV_InstanceID;
};

struct PixelInput
{
    precise float4 pos : SV_Position;
    float3 worldpos : WORLDPOSITION;
    float2 uvsets : UVSETS;
    half4 color : COLOR;
};

// Place a sprite-quad corner (local 2D offset) on a camera-facing billboard centered at 'center'.
float3 BillboardCorner(float3 center, float2 local_offset, ShaderCamera camera)
{
    float3 view = camera.position - center;
    view = dot(view, view) > FLT_EPSILON ? normalize(view) : float3(0.0f, 0.0f, -1.0f);
    const float3 up = abs(view.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
    float3 right = cross(up, view);
    right = dot(right, right) > FLT_EPSILON ? normalize(right) : float3(1.0f, 0.0f, 0.0f);
    const float3 billboard_up = cross(view, right);
    return center + right * local_offset.x + billboard_up * local_offset.y;
}

#endif
