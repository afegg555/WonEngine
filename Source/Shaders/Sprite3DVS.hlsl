#include "SpriteCommon.hlsli"

PixelInput main(VertexInput input)
{
    const ShaderSprite sprite = bindless_structured_sprite[DescriptorIndex(push.sprite_buffer_descriptor)][push.sprite_index];

    PixelInput output;
    ShaderCamera camera = GetCamera();

    const float2 quad_position = GetQuadPosition(input.vertex_id);
    const float2 quad_uv = GetQuadUV(input.vertex_id);
    const float2 local_position_2d = (quad_position - sprite.size_pivot.zw) * sprite.size_pivot.xy;

    const uint sprite_flags = sprite.GetFlags();
    half4 color = half4(1.0h, 1.0h, 1.0h, 1.0h);
    float4 world_position;

    if ((sprite_flags & SHADER_SPRITE_FLAG_PARTICLE) != 0) // this is ok, in cpu side we will seperate draw calls for particle and non-particle sprite
    {
        // CPU particle: per-particle position + color come from the bindless float4 buffer,
        // laid out as interleaved [position, color] pairs indexed by instance_index.
        StructuredBuffer<float4> particle_buffer = bindless_buffers_float4[DescriptorIndex(GetScene().particlebuffer)];
        const float3 center = particle_buffer[sprite.instance_index * 2u].xyz;
        color = (half4)particle_buffer[sprite.instance_index * 2u + 1u];
        world_position = float4(BillboardCorner(center, local_position_2d, camera), 1.0f);
    }
    else
    {
        ShaderInstance instance = GetInstance(sprite.instance_index);
        if ((sprite_flags & SHADER_SPRITE_FLAG_BILLBOARD) != 0)
        {
            const float3 center = mul(instance.world_transform, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;
            world_position = float4(BillboardCorner(center, local_position_2d, camera), 1.0f);
        }
        else
        {
            world_position = mul(instance.world_transform, float4(local_position_2d.x, local_position_2d.y, 0.0f, 1.0f));
        }
    }

    output.worldpos = world_position.xyz;
    output.pos = mul(camera.view_projection, world_position);
    output.uvsets = lerp(sprite.uv_rect.xy, sprite.uv_rect.zw, quad_uv);
    output.color = color;
    return output;
}
