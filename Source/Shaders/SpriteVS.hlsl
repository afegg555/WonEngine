#include "SpriteCommon.hlsli"

float2 GetQuadPosition(uint vertex_id)
{
    // Sprite faces +Z. Vertex order is TL, TR, BL, BL, TR, BR.
    static const float2 positions[6] = {
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
    static const float2 uvs[6] = {
        float2(0.0f, 0.0f),
        float2(1.0f, 0.0f),
        float2(0.0f, 1.0f),

        float2(0.0f, 1.0f),
        float2(1.0f, 0.0f),
        float2(1.0f, 1.0f),
    };

    return uvs[vertex_id % 6];
}

PixelInput main(VertexInput input)
{
    PixelInput output;
    ShaderInstance instance = GetInstance(push.instance_index);
    ShaderCamera camera = GetCamera();

    const float2 quad_position = GetQuadPosition(input.vertex_id);
    const float2 quad_uv = GetQuadUV(input.vertex_id);
    const float2 local_position_2d = (quad_position - push.size_pivot.zw) * push.size_pivot.xy;

    float4 world_position;
    if ((push.flags & SHADER_SPRITE_FLAG_BILLBOARD) != 0)
    {
        const float3 center = mul(instance.world_transform, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
        float3 view = camera.position - center;
        view = dot(view, view) > FLT_EPSILON ? normalize(view) : float3(0.0f, 0.0f, -1.0f);
        const float3 up = abs(view.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f);
        float3 right = cross(up, view);
        right = dot(right, right) > FLT_EPSILON ? normalize(right) : float3(1.0f, 0.0f, 0.0f);
        const float3 billboard_up = cross(view, right);
        world_position = float4(center + right * local_position_2d.x + billboard_up * local_position_2d.y, 1.0f);
    }
    else
    {
        world_position = mul(instance.world_transform, float4(local_position_2d.x, local_position_2d.y, 0.0f, 1.0f));
    }

    output.worldpos = world_position.xyz;
    output.pos = mul(camera.view_projection, world_position);
    output.uvsets = lerp(push.uv_rect.xy, push.uv_rect.zw, quad_uv);
    output.color = half4(1.0h, 1.0h, 1.0h, 1.0h);
    return output;
}
