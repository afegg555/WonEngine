#ifndef WON_COMMON
#define WON_COMMON

inline int DescriptorIndex(in int descriptor_index)
{
    return descriptor_index;
}

#define PI 3.14159265358979323846
#define SQRT2 1.41421356237309504880
#define FLT_MAX 3.402823466e+38
#define FLT_EPSILON 1.192092896e-07
#define GOLDEN_RATIO 1.6180339887
#define MEDIUMP_FLT_MAX 65504.0
#define saturateMediump(x) min(x, MEDIUMP_FLT_MAX)

//	Note: when using -enable-16bit-types compile flag, half will be always FP16
#define half min16float
#define half2 min16float2
#define half3 min16float3
#define half4 min16float4
#define half3x3 min16float3x3
#define half3x4 min16float3x4
#define half4x4 min16float4x4

// https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signature-limits
// maximum size of a root signature is 64 DWORDs
// Descriptor tables cost 1 DWORD each.
// Root constants cost 1 DWORD each, since they are 32-bit values.
// Root descriptors (64-bit GPU virtual addresses) cost 2 DWORDs each.
// Static samplers do not have any cost in the size of the root signature

#define DEFAULT_ROOTSIGNATURE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "RootConstants(num32BitConstants = 11, b999), " \
    "CBV(b0), " \
    "CBV(b1), " \
    "DescriptorTable( " \
        "CBV(b2, numDescriptors = 12, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
        "SRV(t0, numDescriptors = 16, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE)," \
        "UAV(u0, numDescriptors = 16, flags = DESCRIPTORS_VOLATILE | DATA_STATIC_WHILE_SET_AT_EXECUTE)" \
    ")," \
	"DescriptorTable( " \
		"Sampler(s0, offset = 0, numDescriptors = 8, flags = DESCRIPTORS_VOLATILE)" \
	")," \
	"DescriptorTable(" \
		"Sampler(s0, space = 1, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE)" \
	")," \
    "DescriptorTable(" \
		"SRV(t0, space = 2, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 3, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 4, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 5, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 6, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 7, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 8, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 9, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 10, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 11, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 12, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 13, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 14, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 15, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 16, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 17, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 18, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 19, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"SRV(t0, space = 20, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 100, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 101, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 102, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 103, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 104, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 105, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 106, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 107, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 108, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 109, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 110, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 111, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 112, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 113, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 114, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
		"UAV(u0, space = 115, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)," \
        "SRV(t0, space = 200, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 201, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 202, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 203, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 204, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 205, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 206, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 207, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
        "SRV(t0, space = 208, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE) " \
    "), " \
    "StaticSampler(s100, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
	"StaticSampler(s101, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
	"StaticSampler(s102, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_LINEAR)," \
	"StaticSampler(s103, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_MIN_MAG_MIP_POINT)," \
	"StaticSampler(s104, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_MIN_MAG_MIP_POINT)," \
	"StaticSampler(s105, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_MIN_MAG_MIP_POINT)," \
	"StaticSampler(s106, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
	"StaticSampler(s107, addressU = TEXTURE_ADDRESS_WRAP, addressV = TEXTURE_ADDRESS_WRAP, addressW = TEXTURE_ADDRESS_WRAP, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \
	"StaticSampler(s108, addressU = TEXTURE_ADDRESS_MIRROR, addressV = TEXTURE_ADDRESS_MIRROR, addressW = TEXTURE_ADDRESS_MIRROR, filter = FILTER_ANISOTROPIC, maxAnisotropy = 16)," \

inline uint2 PackHalf4(in float4 value)
{
    uint2 retVal = 0;
    retVal.x = f32tof16(value.x) | (f32tof16(value.y) << 16u);
    retVal.y = f32tof16(value.z) | (f32tof16(value.w) << 16u);
    return retVal;
}

inline half4 UnpackHalf4(in uint2 value)
{
    half4 retVal;
    retVal.x = (half) f16tof32(value.x);
    retVal.y = (half) f16tof32(value.x >> 16u);
    retVal.z = (half) f16tof32(value.y);
    retVal.w = (half) f16tof32(value.y >> 16u);
    return retVal;
}

// Unpack an 8-bit-per-channel color packed as 0xRRGGBBAA (R in the most significant byte).
inline float4 UnpackRGBA8(in uint value)
{
    return float4((value >> 24u) & 0xffu,
                  (value >> 16u) & 0xffu,
                  (value >> 8u) & 0xffu,
                  value & 0xffu) / 255.0f;
}

inline float RadicalInverseVdC(uint bits)
{
    // invert all bits and normalize by 2^32 (result in range[0,1))
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f;
}

inline float2 Hammersley(uint index, uint count)
{
    // result in range[0,1)
    return float2((float(index) + 0.5f) / max(float(count), 1.0f), RadicalInverseVdC(index));
}

inline float3 SampleSphere(float2 xi)
{
    float z = 1.0f - 2.0f * xi.x;
    float radius = sqrt(saturate(1.0f - z * z));
    float phi = 2.0f * PI * xi.y;
    return float3(cos(phi) * radius, sin(phi) * radius, z);
}

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

SamplerState bindless_samplers[] : register(s0, space1);

Texture2D bindless_textures[] : register(t0, space2);
ByteAddressBuffer bindless_buffers[] : register(t0, space3);
StructuredBuffer<uint> bindless_buffers_uint[] : register(t0, space4);
StructuredBuffer<uint2> bindless_buffers_uint2[] : register(t0, space5);
StructuredBuffer<uint3> bindless_buffers_uint3[] : register(t0, space6);
StructuredBuffer<uint4> bindless_buffers_uint4[] : register(t0, space7);
StructuredBuffer<float> bindless_buffers_float[] : register(t0, space8);
StructuredBuffer<float2> bindless_buffers_float2[] : register(t0, space9);
StructuredBuffer<float3> bindless_buffers_float3[] : register(t0, space10);
StructuredBuffer<float4> bindless_buffers_float4[] : register(t0, space11);
StructuredBuffer<half> bindless_buffers_half[] : register(t0, space12);
StructuredBuffer<half2> bindless_buffers_half2[] : register(t0, space13);
StructuredBuffer<half3> bindless_buffers_half3[] : register(t0, space14);
StructuredBuffer<half4> bindless_buffers_half4[] : register(t0, space15);
Texture2DArray bindless_textures2DArray[] : register(t0, space16);
TextureCube bindless_cubemaps[] : register(t0, space17);
TextureCubeArray bindless_cubearrays[] : register(t0, space18);
Texture3D bindless_textures3D[] : register(t0, space19);
Texture2D<half4> bindless_textures_half4[] : register(space20);

RWTexture2D<float4> bindless_rwtextures[] : register(u0, space100);
RWByteAddressBuffer bindless_rwbuffers[] : register(u0, space101);
RWStructuredBuffer<uint> bindless_rwbuffers_uint[] : register(u0, space102);
RWStructuredBuffer<uint2> bindless_rwbuffers_uint2[] : register(u0, space103);
RWStructuredBuffer<uint3> bindless_rwbuffers_uint3[] : register(u0, space104);
RWStructuredBuffer<uint4> bindless_rwbuffers_uint4[] : register(u0, space105);
RWStructuredBuffer<float> bindless_rwbuffers_float[] : register(u0, space106);
RWStructuredBuffer<float2> bindless_rwbuffers_float2[] : register(u0, space107);
RWStructuredBuffer<float3> bindless_rwbuffers_float3[] : register(u0, space108);
RWStructuredBuffer<float4> bindless_rwbuffers_float4[] : register(u0, space109);
RWTexture2DArray<float4> bindless_rwtextures2DArray[] : register(u0, space110);
RWTexture3D<float4> bindless_rwtextures3D[] : register(u0, space111);
RWTexture2D<uint> bindless_rwtextures_uint[] : register(u0, space112);
RWTexture2D<uint2> bindless_rwtextures_uint2[] : register(u0, space113);
RWTexture2D<uint3> bindless_rwtextures_uint3[] : register(u0, space114);
RWTexture2D<uint4> bindless_rwtextures_uint4[] : register(u0, space115);

#include "ShaderInterop_Renderer.h"

StructuredBuffer<ShaderInstance> bindless_structured_instance[] : register(t0, space200);
StructuredBuffer<ShaderGeometry> bindless_structured_geometry[] : register(t0, space201);
StructuredBuffer<ShaderMaterial> bindless_structured_material[] : register(t0, space202);
StructuredBuffer<ShaderLight> bindless_structured_light[] : register(t0, space203);
StructuredBuffer<ShaderShadowCascade> bindless_structured_shadow_cascade[] : register(t0, space204);
StructuredBuffer<ShaderBVHNode> bindless_structured_bvh_node[] : register(t0, space205);
StructuredBuffer<ShaderBVHPrimitive> bindless_structured_bvh_primitive[] : register(t0, space206);
StructuredBuffer<ShaderBVHInstance> bindless_structured_bvh_instance[] : register(t0, space207);
StructuredBuffer<ShaderDecal> bindless_structured_decal[] : register(t0, space208);

// static samplers
SamplerState sampler_linear_clamp : register(s100);
SamplerState sampler_linear_wrap : register(s101);
SamplerState sampler_linear_mirror : register(s102);
SamplerState sampler_point_clamp : register(s103);
SamplerState sampler_point_wrap : register(s104);
SamplerState sampler_point_mirror : register(s105);
SamplerState sampler_aniso_clamp : register(s106);
SamplerState sampler_aniso_wrap : register(s107);
SamplerState sampler_aniso_mirror : register(s108);

inline ShaderFrame GetFrame()
{
    return g_frame;
}

inline ShaderScene GetScene()
{
    return g_frame.scene;
}

inline ShaderEnvironment GetEnvironment()
{
    return g_frame.environment;
}

inline ShaderDDGIVolume GetDDGIVolume()
{
    return g_frame.ddgi_volume;
}

inline ShaderCamera GetCamera()
{
    return g_camera;
}

inline ShaderInstance GetInstance(uint instance_index)
{
    return bindless_structured_instance[DescriptorIndex(GetScene().instancebuffer)][instance_index];
}

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
inline ShaderInstance GetInstance()
{
    return GetInstance(push.draw_offset);
}
#endif

inline ShaderGeometry GetGeometry(uint geometry_index)
{
    return bindless_structured_geometry[DescriptorIndex(GetScene().geometrybuffer)][geometry_index];
}

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
inline ShaderGeometry GetGeometry()
{
    return GetGeometry(push.geometry_index);
}
#endif

inline ShaderMaterial GetMaterial(uint material_index)
{
    return bindless_structured_material[DescriptorIndex(GetScene().materialbuffer)][material_index];
}

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
inline ShaderMaterial GetMaterial()
{
    return GetMaterial(push.material_index);
}
#endif

inline ShaderBVHNode GetBVHNodeFromBuffer(int node_buffer, uint node_index)
{
    return bindless_structured_bvh_node[DescriptorIndex(node_buffer)][node_index];
}

inline ShaderBVHNode GetBVHNode(uint node_index)
{
    return GetBVHNodeFromBuffer(GetScene().bvh_node_buffer, node_index);
}

inline ShaderBVHPrimitive GetBVHPrimitiveFromBuffer(int primitive_buffer, uint primitive_index)
{
    return bindless_structured_bvh_primitive[DescriptorIndex(primitive_buffer)][primitive_index];
}

inline ShaderBVHInstance GetBVHInstance(uint instance_index)
{
    return bindless_structured_bvh_instance[DescriptorIndex(GetScene().bvh_instance_buffer)][instance_index];
}

inline ShaderLightIterator lights(uint bucket_index = 0)
{
    ShaderLightIterator iter;
    iter.value = GetScene().lights[bucket_index];
    return iter;
}

inline ShaderLight GetLight(uint light_index)
{
    return bindless_structured_light[DescriptorIndex(GetScene().lightbuffer)][light_index];
}

inline ShaderShadowCascade GetShadowCascade(uint cascade_index)
{
    return bindless_structured_shadow_cascade[DescriptorIndex(GetScene().shadow_cascade_buffer)][cascade_index];
}

#endif // WON_COMMON
