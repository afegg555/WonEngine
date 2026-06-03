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

    CGU_FLOAT block_r[BLOCK_SIZE_4X4];
    CGU_FLOAT block_g[BLOCK_SIZE_4X4];
    LoadBCChannelBlock(block_id, block_r, block_g);

    const uint4 compressed_block = CompressBlockBC5_UNORM(block_r, block_g, GetBCCompressQuality());
    StoreBCBlock4(GetBCBlockIndex(block_id), compressed_block);
}
