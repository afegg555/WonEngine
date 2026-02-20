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
    "DescriptorTable(SRV(t0, space = 200, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 201, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 202, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 203, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 204, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 205, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE)), " \
    "DescriptorTable(SRV(t0, space = 206, offset = 0, numDescriptors = unbounded, flags = DESCRIPTORS_VOLATILE | DATA_VOLATILE))"

StructuredBuffer<ShaderObject> bindless_structured_object[] : register(t0, space200);
StructuredBuffer<ShaderGeometry> bindless_structured_geometry[] : register(t0, space201);
StructuredBuffer<ShaderMaterial> bindless_structured_material[] : register(t0, space202);
StructuredBuffer<float3> bindless_structured_position[] : register(t0, space203);
StructuredBuffer<float3> bindless_structured_normal[] : register(t0, space204);
StructuredBuffer<float2> bindless_structured_texcoord[] : register(t0, space205);
StructuredBuffer<uint> bindless_structured_index[] : register(t0, space206);

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

inline ShaderObject GetInstance(uint instance_index)
{
    return bindless_structured_object[DescriptorIndex(GetScene().instancebuffer)][instance_index];
}

inline ShaderGeometry GetGeometry(uint geometry_index)
{
    return bindless_structured_geometry[DescriptorIndex(GetScene().geometrybuffer)][geometry_index];
}

inline ShaderMaterial GetMaterial(uint material_index)
{
    return bindless_structured_material[DescriptorIndex(GetScene().materialbuffer)][material_index];
}

#endif // WON_COMMON
