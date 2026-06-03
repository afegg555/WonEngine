#include "TextureBCCompressCommon.hlsli"

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID)
{
    const uint2 block_id = dispatch_thread_id.xy;
    const uint2 block_count = GetBCBlockCount();
    if (block_id.x >= block_count.x || block_id.y >= block_count.y)
    {
        return;
    }

    CGU_Vec3f block_rgb[BLOCK_SIZE_4X4];
    CGU_FLOAT block_alpha[BLOCK_SIZE_4X4];
    LoadBCColorBlock(block_id, block_rgb, block_alpha);

    const uint4 compressed_block = CompressBlockBC3_UNORM(block_rgb, block_alpha, GetBCCompressQuality(), IsBCCompressSRGB());
    StoreBCBlock4(GetBCBlockIndex(block_id), compressed_block);
}
