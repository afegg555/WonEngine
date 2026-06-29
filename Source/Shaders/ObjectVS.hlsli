#include "ObjectCommon.hlsli"

PixelInput main(VertexInput input)
{
    PixelInput output;
    uint stable_index = bindless_buffers_uint[DescriptorIndex(GetScene().instance_sort_buffer)][push.draw_offset + input.instance_id];
    ShaderInstance instance = GetInstance(stable_index);
    ShaderGeometry geometry = GetGeometry();
    float3 local_position = input.GetPosition();
    
#ifdef OBJECTSHADER_USE_NORMAL
    float3 local_normal = input.GetNormal();
#endif // OBJECTSHADER_USE_NORMAL

#ifdef OBJECTSHADER_USE_TANGENT
    float4 local_tangent = input.GetTangent();
#endif // OBJECTSHADER_USE_TANGENT

    [branch]
    if ((geometry.flags & SHADER_GEOMETRY_FLAG_SKINNED) != 0 && geometry.bone_indices_buffer_descriptor >= 0 && geometry.bone_weights_buffer_descriptor >= 0 && GetScene().bone_matrix_buffer >= 0 && instance.bone_count > 0)
    {
        uint4 bone_indices = bindless_buffers_uint4[DescriptorIndex(geometry.bone_indices_buffer_descriptor)][input.GetVertexID()];
        float4 bone_weights = bindless_buffers_float4[DescriptorIndex(geometry.bone_weights_buffer_descriptor)][input.GetVertexID()];
        StructuredBuffer<float4> bone_matrices = bindless_buffers_float4[DescriptorIndex(GetScene().bone_matrix_buffer)];
        float3 skinned_position = 0.0f;
        float applied_weight = 0.0f;

#ifdef OBJECTSHADER_USE_NORMAL
        float3 skinned_normal = 0.0f;
#endif // OBJECTSHADER_USE_NORMAL

#ifdef OBJECTSHADER_USE_TANGENT
        float3 skinned_tangent = 0.0f;
#endif // OBJECTSHADER_USE_TANGENT

        [unroll]
        for (uint influence_index = 0; influence_index < 4; ++influence_index)
        {
            const uint bone_index = bone_indices[influence_index];
            const float bone_weight = bone_weights[influence_index];
            if (bone_weight <= 0.0f || bone_index >= instance.bone_count)
            {
                continue;
            }

            const uint matrix_offset = (instance.bone_matrix_offset + bone_index) * 4;
            float4x4 bone_matrix = float4x4(
                bone_matrices[matrix_offset],
                bone_matrices[matrix_offset + 1],
                bone_matrices[matrix_offset + 2],
                bone_matrices[matrix_offset + 3]);

            skinned_position += mul(bone_matrix, float4(local_position, 1.0f)).xyz * bone_weight;

#ifdef OBJECTSHADER_USE_NORMAL
            skinned_normal += mul((float3x3)bone_matrix, local_normal) * bone_weight;
#endif // OBJECTSHADER_USE_NORMAL

#ifdef OBJECTSHADER_USE_TANGENT
            skinned_tangent += mul((float3x3)bone_matrix, local_tangent.xyz) * bone_weight;
#endif // OBJECTSHADER_USE_TANGENT

            applied_weight += bone_weight;
        }

        [branch]
        if (applied_weight > 0.0f)
        {
            const float inv_applied_weight = rcp(applied_weight);
            local_position = skinned_position * inv_applied_weight;

#ifdef OBJECTSHADER_USE_NORMAL
            local_normal = skinned_normal * inv_applied_weight;
#endif // OBJECTSHADER_USE_NORMAL

#ifdef OBJECTSHADER_USE_TANGENT
            local_tangent.xyz = skinned_tangent * inv_applied_weight;
#endif // OBJECTSHADER_USE_TANGENT
        }
    }

    output.pos = mul(instance.world_transform, float4(local_position, 1.0f));
    ShaderCamera camera = GetCamera();
    output.worldpos = output.pos.xyz;
    output.pos = mul(camera.view_projection, output.pos);
    
#ifdef OBJECTSHADER_USE_COLOR
    output.color = half4(1.0, 1.0, 1.0, 1.0);
    
	[branch]
    if (GetMaterial().IsUsingVertexColors())
    {
        output.color *= input.GetVertexColor();
    }
    
#endif // OBJECTSHADER_USE_COLOR

    float3x3 normal_mat = float3x3(instance.normal_transform_row0, instance.normal_transform_row1, instance.normal_transform_row2);
    
#ifdef OBJECTSHADER_USE_UVSETS
	output.uvsets = input.GetUVSets();
    // https://learn.microsoft.com/ko-kr/windows/win32/direct3dhlsl/mad
	// efficient multiply and add
#endif // OBJECTSHADER_USE_UVSETS

#ifdef OBJECTSHADER_USE_NORMAL
	output.nor = local_normal;
    output.nor = normalize(mul(normal_mat, output.nor));
#endif // OBJECTSHADER_USE_NORMAL

#ifdef OBJECTSHADER_USE_TANGENT
	output.tan = local_tangent;
    output.tan.xyz = normalize(mul(normal_mat, output.tan.xyz));
#endif // OBJECTSHADER_USE_TANGENT
	
    return output;
}
