#ifndef OBJECT_COMMON
#define OBJECT_COMMON

#include "Common.hlsli"

#define sampler_objectshader sampler_linear_wrap // test
#define sampler_objectshader_clamp sampler_linear_clamp // test

// layout for prepass
#ifdef OBJECTSHADER_LAYOUT_PREPASS
#define PREPASS
#endif // OBJECTSHADER_LAYOUT_PREPASS

// layout for common passes
#ifdef OBJECTSHADER_LAYOUT_COMMON
#define OBJECTSHADER_USE_UVSETS
#define OBJECTSHADER_USE_COLOR
#define OBJECTSHADER_USE_NORMAL
#define OBJECTSHADER_USE_TANGENT
#define OBJECTSHADER_USE_EMISSIVE
#endif // OBJECTSHADER_LAYOUT_COMMON

struct VertexInput
{
    uint vertex_id : SV_VertexID;
    uint instance_id : SV_InstanceID;

    uint GetVertexID()
    {
        return vertex_id;
    }

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
    float3 GetPosition()
    {
        return bindless_buffers_float3[DescriptorIndex(GetGeometry().position_buffer_descriptor)][GetVertexID()];
    }

    half4 GetVertexColor()
    {
		[branch]
        if (GetGeometry().color_buffer_descriptor < 0)
            return 1;
        return bindless_buffers_half4[DescriptorIndex(GetGeometry().color_buffer_descriptor)][GetVertexID()];
    }
	
    float3 GetNormal()
    {
		[branch]
        if (GetGeometry().normal_buffer_descriptor < 0)
            return 1;
        return bindless_buffers_float3[DescriptorIndex(GetGeometry().normal_buffer_descriptor)][GetVertexID()];
    }
    
    float2 GetUVSets()
    {
		[branch]
        if (GetGeometry().texcoord_buffer_descriptor < 0)
            return 1;
        return bindless_buffers_float2[DescriptorIndex(GetGeometry().texcoord_buffer_descriptor)][GetVertexID()];
    }
    
    float4 GetTangent()
    {
		[branch]
        if (GetGeometry().tangent_buffer_descriptor < 0)
            return 1;
        return bindless_buffers_float4[DescriptorIndex(GetGeometry().tangent_buffer_descriptor)][GetVertexID()];
    }
#endif
};

struct PixelInput
{
    precise float4 pos : SV_Position;
    float3 worldpos : WORLDPOSITION;
    
#if defined(PREPASS)
	uint primitive_id : PRIMITIVEID;
#endif // defined(PREPASS)

#ifdef OBJECTSHADER_USE_UVSETS
	float2 uvsets : UVSETS;
#endif // OBJECTSHADER_USE_UVSETS

#ifdef OBJECTSHADER_USE_TANGENT
	float4 tan : TANGENT;
#endif // OBJECTSHADER_USE_TANGENT

#ifdef OBJECTSHADER_USE_NORMAL
	float3 nor : NORMAL;
#endif // OBJECTSHADER_USE_NORMAL

#ifdef OBJECTSHADER_USE_COLOR
	half4 color : COLOR;
#endif // OBJECTSHADER_USE_COLOR
    
    inline float3 GetViewVector()
    {
        ShaderCamera camera = GetCamera();

        return normalize(camera.position - worldpos);
    }
};

#endif // OBJECT_COMMON

