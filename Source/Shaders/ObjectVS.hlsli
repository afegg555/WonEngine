#include "ObjectCommon.hlsli"

PixelInput main(VertexInput input)
{
    PixelInput output;
    output.pos = float4(input.GetPosition(), 1.f);
    output.pos = mul(GetInstance().world_transform, output.pos);
    
    ShaderCamera camera = GetCamera();

    output.pos = mul(camera.view_projection, output.pos);
    
#ifdef OBJECTSHADER_USE_COLOR
    output.color = half4(1.0, 1.0, 1.0, 1.0);
    
	[branch]
    if (GetMaterial().IsUsingVertexColors())
    {
        output.color *= input.GetVertexColor();
    }
    
#endif // OBJECTSHADER_USE_COLOR

    float3x3 normal_mat = GetInstance().normal_transform;
    
#ifdef OBJECTSHADER_USE_UVSETS
	output.uvsets = input.GetUVSets();
    // https://learn.microsoft.com/ko-kr/windows/win32/direct3dhlsl/mad
	// efficient multiply and add
#endif // OBJECTSHADER_USE_UVSETS

#ifdef OBJECTSHADER_USE_NORMAL
	output.nor = input.GetNormal();
    output.nor = normalize(mul(normal_mat, output.nor));
#endif // OBJECTSHADER_USE_NORMAL

#ifdef OBJECTSHADER_USE_TANGENT
	output.tan = input.GetTangent();
    output.tan.xyz = normalize(mul(normal_mat, output.tan.xyz));
#endif // OBJECTSHADER_USE_TANGENT
	
    return output;
}