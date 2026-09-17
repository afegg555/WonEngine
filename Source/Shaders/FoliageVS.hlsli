#include "ObjectCommon.hlsli"

PixelInput main(VertexInput input)
{
    PixelInput output;

    const uint foliage_instance_buffer = DescriptorIndex(GetScene().foliage_instance_buffer);
    const uint base = (push.instance_offset + input.instance_id) * 2;
    const float4 position_scale = bindless_buffers_float4[foliage_instance_buffer][base + 0];
    const float4 rotation = bindless_buffers_float4[foliage_instance_buffer][base + 1];

    const float qx = rotation.x, qy = rotation.y, qz = rotation.z, qw = rotation.w;
    const float3x3 rotation_matrix = float3x3(
        1.0f - 2.0f * (qy * qy + qz * qz), 2.0f * (qx * qy - qw * qz),        2.0f * (qx * qz + qw * qy),
        2.0f * (qx * qy + qw * qz),        1.0f - 2.0f * (qx * qx + qz * qz), 2.0f * (qy * qz - qw * qx),
        2.0f * (qx * qz - qw * qy),        2.0f * (qy * qz + qw * qx),        1.0f - 2.0f * (qx * qx + qy * qy));
    const float scale = position_scale.w;
    const float4x4 world_transform = float4x4(
        rotation_matrix._11 * scale, rotation_matrix._12 * scale, rotation_matrix._13 * scale, position_scale.x,
        rotation_matrix._21 * scale, rotation_matrix._22 * scale, rotation_matrix._23 * scale, position_scale.y,
        rotation_matrix._31 * scale, rotation_matrix._32 * scale, rotation_matrix._33 * scale, position_scale.z,
        0.0f, 0.0f, 0.0f, 1.0f);

    const float3 local_position = input.GetPosition();
    output.pos = mul(world_transform, float4(local_position, 1.0f));
    ShaderCamera camera = GetCamera();
    output.worldpos = output.pos.xyz;
    output.pos = mul(camera.view_projection, output.pos);

#ifdef OBJECTSHADER_USE_UVSETS
    output.uvsets = input.GetUVSets();
#endif

#ifdef OBJECTSHADER_USE_COLOR
    output.color = half4(1.0, 1.0, 1.0, 1.0);
    [branch]
    if (GetMaterial().IsUsingVertexColors())
    {
        output.color *= input.GetVertexColor();
    }
#endif

#ifdef OBJECTSHADER_USE_NORMAL
    output.nor = normalize(mul(rotation_matrix, input.GetNormal()));
#endif

#ifdef OBJECTSHADER_USE_TANGENT
    output.tan = input.GetTangent();
    output.tan.xyz = normalize(mul(rotation_matrix, output.tan.xyz));
#endif

    return output;
}
