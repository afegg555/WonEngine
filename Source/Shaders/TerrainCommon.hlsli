#ifndef TERRAIN_COMMON
#define TERRAIN_COMMON

#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"

PUSHCONSTANT(terrainpush, TerrainPushConstants);

inline ShaderGeometry GetGeometry()
{
    return GetGeometry(terrainpush.geometry_index);
}

struct TerrainVertexInput
{
    uint vertex_id : SV_VertexID;
    uint instance_id : SV_InstanceID;
};

struct TerrainPixelInput
{
    precise float4 pos : SV_Position;
    float3 worldpos : WORLDPOSITION;
    float2 uvsets : UVSETS;
    float3 nor : NORMAL;
    half4 color : COLOR;

    inline float3 GetViewVector()
    {
        return normalize(GetCamera().position - worldpos);
    }
};

inline uint GetTerrainStreamVertexID(uint vertex_id)
{
    return vertex_id + GetFrame().frame_slot * GetGeometry().dynamic_stream_stride;
}

inline uint GetTerrainTransformIndex(uint instance_id)
{
    return bindless_buffers_uint[DescriptorIndex(GetView().transform_index_buffer)][terrainpush.instance_offset + instance_id];
}

inline float3 GetTerrainPosition(uint vertex_id)
{
    return bindless_buffers_float3[DescriptorIndex(GetGeometry().position_buffer_descriptor)][GetTerrainStreamVertexID(vertex_id)];
}

#endif
