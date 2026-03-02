#ifndef WON_COMMON
#define WON_COMMON

#include "ShaderInterop_Renderer.h"

inline int DescriptorIndex(in int descriptor_index)
{
    return descriptor_index;
}

#define DEFAULT_ROOTSIGNATURE \
    "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT), " \
    "RootConstants(num32BitConstants = 4, b999), " \
    "CBV(b0), " \
    "CBV(b1), " \
    "DescriptorTable(" \
                    "SRV(t0, space = 200, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
                    "SRV(t0, space = 201, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
                    "SRV(t0, space = 202, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
                    "SRV(t0, space = 203, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
                    "SRV(t0, space = 204, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
                    "SRV(t0, space = 205, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
                    "SRV(t0, space = 206, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE), " \
                    "SRV(t0, space = 207, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)" \
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

StructuredBuffer<ShaderInstance> bindless_structured_instance[] : register(t0, space200);
StructuredBuffer<ShaderGeometry> bindless_structured_geometry[] : register(t0, space201);
StructuredBuffer<ShaderMaterial> bindless_structured_material[] : register(t0, space202);
StructuredBuffer<float3> bindless_structured_position[] : register(t0, space203);
StructuredBuffer<float3> bindless_structured_normal[] : register(t0, space204);
StructuredBuffer<float2> bindless_structured_texcoord[] : register(t0, space205);
StructuredBuffer<uint> bindless_structured_index[] : register(t0, space206);
Texture2D<float4> bindless_textures[] : register(t0, space207);

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

inline ShaderCamera GetCamera()
{
    return g_camera;
}

inline ShaderInstance GetInstance(uint instance_index)
{
    return bindless_structured_instance[DescriptorIndex(GetScene().instancebuffer)][instance_index];
}

inline ShaderInstance GetInstance()
{
    return GetInstance(push.instance_index);

}

inline ShaderGeometry GetGeometry(uint geometry_index)
{
    return bindless_structured_geometry[DescriptorIndex(GetScene().geometrybuffer)][geometry_index];
}

inline ShaderGeometry GetGeometry()
{
    return GetGeometry(push.geometry_index);
}

inline ShaderMaterial GetMaterial(uint material_index)
{
    return bindless_structured_material[DescriptorIndex(GetScene().materialbuffer)][material_index];
}

inline ShaderMaterial GetMaterial()
{
    return GetMaterial(push.material_index);
}

#endif // WON_COMMON
