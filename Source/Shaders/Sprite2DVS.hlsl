#include "SpriteCommon.hlsli"

PixelInput main(VertexInput input)
{
    const ShaderSprite sprite = bindless_structured_sprite[DescriptorIndex(push.sprite_buffer_descriptor)][push.sprite_index];

    PixelInput output;
    ShaderCamera camera = GetCamera();

    const float2 quad_position = GetQuadUV(input.vertex_id);
    const float2 quad_uv = GetQuadUV(input.vertex_id);
    const float2 normalized_position = float2(f16tof32(sprite.instance_index & 0xFFFFu), f16tof32(sprite.instance_index >> 16u));
    const float2 viewport_size = float2(camera.internal_resolution);
    const float2 pixel_position = normalized_position * viewport_size;
    const float2 local_position = (quad_position - sprite.size_pivot.zw) * sprite.size_pivot.xy;
    const float2 screen_position = pixel_position + local_position;
    const float2 ndc_position = float2(screen_position.x / viewport_size.x * 2.0f - 1.0f, 1.0f - screen_position.y / viewport_size.y * 2.0f);

    output.worldpos = float3(0.0f, 0.0f, 0.0f);
    output.pos = float4(ndc_position, 0.0f, 1.0f);
    output.uvsets = lerp(sprite.uv_rect.xy, sprite.uv_rect.zw, quad_uv);
    output.color = half4(1.0h, 1.0h, 1.0h, 1.0h);
    return output;
}
