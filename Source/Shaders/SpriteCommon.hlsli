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

float2 GetQuadPosition(uint vertex_id)
{
    // Sprite faces +Z. Vertex order is TL, TR, BL, BL, TR, BR.
    static const float2 positions[6] =
    {
        float2(1.0f, 1.0f),
        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),

        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),
        float2(0.0f, 0.0f),
    };

    return positions[vertex_id % 6];
}

float2 GetQuadUV(uint vertex_id)
{
    static const float2 uvs[6] =
    {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),

        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
    };

    return uvs[vertex_id % 6];
}

#endif
