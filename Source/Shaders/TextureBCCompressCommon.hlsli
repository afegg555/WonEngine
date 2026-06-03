#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_TEXTURE_BC_COMPRESS_PUSHCONSTANT
#include "ShaderInterop_Utility.h"

#define ASPM_HLSL
#include "Compressonator/bcn_common_kernel.h"


float GetBCCompressQuality()
{
    return bccompresspush.quality > 0.0f ? bccompresspush.quality : CMP_QUALITY0;
}

bool IsBCCompressSRGB()
{
    return (bccompresspush.flags & TEXTURE_BC_COMPRESS_FLAGS_IS_SRGB) != 0;
}

uint2 GetBCBlockCount()
{
    return uint2((bccompresspush.width + 3u) / 4u, (bccompresspush.height + 3u) / 4u);
}

uint GetBCBlockIndex(uint2 block_id)
{
    return block_id.y * GetBCBlockCount().x + block_id.x;
}

void LoadBCColorBlock(uint2 block_id, out CGU_Vec3f block_rgb[BLOCK_SIZE_4X4], out CGU_FLOAT block_alpha[BLOCK_SIZE_4X4])
{
    Texture2D source_texture = bindless_textures[DescriptorIndex((int)bccompresspush.source_srv)];
    const uint2 max_texel = uint2(bccompresspush.width - 1u, bccompresspush.height - 1u);
    [unroll]
    for (uint y = 0u; y < 4u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 4u; ++x)
        {
            const uint index = y * 4u + x;
            const float4 texel = source_texture.Load(int3(min(block_id * 4u + uint2(x, y), max_texel), 0));
            block_rgb[index] = texel.rgb;
            block_alpha[index] = texel.a;
        }
    }
}

void LoadBCChannelBlock(uint2 block_id, out CGU_FLOAT block_r[BLOCK_SIZE_4X4], out CGU_FLOAT block_g[BLOCK_SIZE_4X4])
{
    Texture2D source_texture = bindless_textures[DescriptorIndex((int)bccompresspush.source_srv)];
    const uint2 max_texel = uint2(bccompresspush.width - 1u, bccompresspush.height - 1u);
    [unroll]
    for (uint y = 0u; y < 4u; ++y)
    {
        [unroll]
        for (uint x = 0u; x < 4u; ++x)
        {
            const uint index = y * 4u + x;
            const float4 texel = source_texture.Load(int3(min(block_id * 4u + uint2(x, y), max_texel), 0));
            block_r[index] = texel.r;
            block_g[index] = texel.g;
        }
    }
}

void StoreBCBlock2(uint block_index, uint2 block)
{
    RWStructuredBuffer<uint> output_blocks = bindless_rwbuffers_uint[DescriptorIndex((int)bccompresspush.output_uav)];
    const uint offset = bccompresspush.output_offset + block_index * 2u;
    output_blocks[offset] = block.x;
    output_blocks[offset + 1u] = block.y;
}

void StoreBCBlock4(uint block_index, uint4 block)
{
    RWStructuredBuffer<uint> output_blocks = bindless_rwbuffers_uint[DescriptorIndex((int)bccompresspush.output_uav)];
    const uint offset = bccompresspush.output_offset + block_index * 4u;
    output_blocks[offset] = block.x;
    output_blocks[offset + 1u] = block.y;
    output_blocks[offset + 2u] = block.z;
    output_blocks[offset + 3u] = block.w;
}
