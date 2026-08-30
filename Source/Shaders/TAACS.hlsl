#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"
#define WON_TAA_CONSTANTBUFFER
#include "ShaderInterop_PostProcess.h"

static const float taa_variance_gamma = 1.0f;
static const float taa_depth_reject_relative = 0.05f;
static const float taa_depth_reject_absolute = 0.05f;
static const float taa_disocclusion_spatial_weight = 0.5f;

static const uint taa_tile_size = DISPATCH_THREAD_GROUP_2D + 2; // extended for border
static const uint taa_tile_texel_count = taa_tile_size * taa_tile_size;
static const uint taa_group_thread_count = DISPATCH_THREAD_GROUP_2D * DISPATCH_THREAD_GROUP_2D; // not extended

groupshared float4 taa_tile[taa_tile_texel_count];

// Y : luma
// Co(Chrominance orange) : orange(+) to blue(-)
// Cg(Chrominance green) : green(+) to magenta(-)
float3 RGBToYCoCg(float3 rgb)
{
    return float3(
        0.25f * rgb.r + 0.5f * rgb.g + 0.25f * rgb.b,
        0.5f * rgb.r - 0.5f * rgb.b,
        -0.25f * rgb.r + 0.5f * rgb.g - 0.25f * rgb.b);
}

float3 YCoCgToRGB(float3 ycocg)
{
    const float chroma = ycocg.x - ycocg.z;
    return float3(chroma + ycocg.y, ycocg.x + ycocg.z, chroma - ycocg.y);
}

// temp hdr-ldr conversion to prevent fireflies 
float3 TAALuminanceCompress(float3 color)
{
    return color / (1.0f + Luminance(color));
}

float3 TAALuminanceExpand(float3 color)
{
    return color / max(1.0f - Luminance(color), 1e-4f);
}

float3 TAAClipToAABB(float3 history, float3 minimum, float3 maximum)
{
    const float3 center = 0.5f * (maximum + minimum);
    const float3 extent = max(0.5f * (maximum - minimum), 1e-5f);
    const float3 offset = history - center;
    const float3 unit = abs(offset / extent);
    const float largest = max(unit.x, max(unit.y, unit.z));
    if (largest > 1.0f)
    {
        return center + offset / largest;
    }
    return history;
}

[numthreads(DISPATCH_THREAD_GROUP_2D, DISPATCH_THREAD_GROUP_2D, 1)]
void main(uint3 dispatch_thread_id : SV_DispatchThreadID,
          uint3 group_id : SV_GroupID,
          uint3 group_thread_id : SV_GroupThreadID,
          uint group_index : SV_GroupIndex)
{
    Texture2D current_color = bindless_textures[DescriptorIndex((int)taacb.current_descriptor)];
    const int2 last_pixel = int2(taacb.resolution) - 1;
    const int2 tile_origin = int2(group_id.xy) * DISPATCH_THREAD_GROUP_2D - 1; // -1 for extended border

    for (uint fill = group_index; fill < taa_tile_texel_count; fill += taa_group_thread_count)
    {
        const int2 tile_coord = int2(fill % taa_tile_size, fill / taa_tile_size);
        const float4 texel = current_color.Load(int3(clamp(tile_origin + tile_coord, int2(0, 0), last_pixel), 0));
        taa_tile[fill] = float4(RGBToYCoCg(TAALuminanceCompress(texel.rgb)), texel.a);
    }
    GroupMemoryBarrierWithGroupSync();

    if (dispatch_thread_id.x >= taacb.resolution.x || dispatch_thread_id.y >= taacb.resolution.y)
    {
        return;
    }

    Texture2D history_color = bindless_textures[DescriptorIndex((int)taacb.history_descriptor)];
    Texture2D depth_buffer = bindless_textures[DescriptorIndex((int)taacb.depth_descriptor)];
    Texture2D depth_history = bindless_textures[DescriptorIndex((int)taacb.depth_history_descriptor)];
    RWTexture2D<float4> destination = bindless_rwtextures[DescriptorIndex((int)taacb.output_descriptor)];
    RWTexture2D<float4> history_destination = bindless_rwtextures[DescriptorIndex((int)taacb.history_output_descriptor)];
    RWTexture2D<float4> depth_history_destination = bindless_rwtextures[DescriptorIndex((int)taacb.depth_history_output_descriptor)];

    const int2 pixel = int2(dispatch_thread_id.xy);
    const int2 tile_center = int2(group_thread_id.xy) + 1;
    const float4 center = taa_tile[tile_center.y * taa_tile_size + tile_center.x];
    const float3 current_ycocg = center.rgb;

    float3 neighborhood_min = FLT_MAX;
    float3 neighborhood_max = -FLT_MAX;
    float3 neighborhood_sum = 0.0f;
    float3 neighborhood_squared_sum = 0.0f;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float3 neighbor = taa_tile[(tile_center.y + y) * taa_tile_size + (tile_center.x + x)].rgb;
            neighborhood_min = min(neighborhood_min, neighbor);
            neighborhood_max = max(neighborhood_max, neighbor);
            neighborhood_sum += neighbor;
            neighborhood_squared_sum += neighbor * neighbor;
        }
    }

    const float3 neighborhood_mean = neighborhood_sum / 9.0f;
    const float3 neighborhood_sigma = sqrt(max(neighborhood_squared_sum / 9.0f - neighborhood_mean * neighborhood_mean, 0.0f));
    const float3 clip_min = max(neighborhood_min, neighborhood_mean - taa_variance_gamma * neighborhood_sigma); // intersection range of (mean+-sigma) and (minmax)
    const float3 clip_max = min(neighborhood_max, neighborhood_mean + taa_variance_gamma * neighborhood_sigma);

    const float2 resolution = float2(taacb.resolution);
    const float2 rcp_resolution = 1.0f / resolution;
    const float2 screen_uv = (float2(pixel) + 0.5f) * rcp_resolution;
    const float device_depth = depth_buffer.Load(int3(pixel, 0)).r;
    const float3 world_position = NDCToWorld(ScreenUVToNDC(screen_uv) + float2(GetCamera().jitter_x, GetCamera().jitter_y), device_depth); // inv_view_projection contains jitter, so the result is unjittered world pos
    const float view_depth = dot(world_position - GetCamera().position, GetCamera().forward);

    float blend = 0.0f;
    float3 history_ycocg = current_ycocg;
    if (taacb.history_valid != 0) // has history
    {
        const float4 previous_clip = mul(GetCamera().previous_view_projection, float4(world_position, 1.0f)); // prev clip space position of this world pos
        if (previous_clip.w > 0.0f) // ahead of camera
        {
            const float2 previous_ndc = previous_clip.xy / previous_clip.w;
            const float2 previous_uv = NDCToScreenUV(previous_ndc);
            if (all(previous_uv > 0.0f) && all(previous_uv < 1.0f)) // in screen
            {
                const float previous_view_depth = dot(world_position - GetCamera().previous_position, GetCamera().previous_forward);
                const float stored_view_depth = depth_history.SampleLevel(sampler_point_clamp, previous_uv, 0).r; // prev position
                const float depth_tolerance = taa_depth_reject_absolute + taa_depth_reject_relative * abs(previous_view_depth);
                if (abs(previous_view_depth - stored_view_depth) <= depth_tolerance)
                {
                    const float3 history_rgb = SampleTextureCatmullRom5Tap(history_color, previous_uv, resolution);
                    history_ycocg = TAAClipToAABB(RGBToYCoCg(TAALuminanceCompress(history_rgb)), clip_min, clip_max);
                    blend = taacb.history_blend;
                }
                // blend = 0 for disocclusion
            }
        }
    }

    const float3 resolved_ycocg = blend > 0.0f
        ? lerp(current_ycocg, history_ycocg, blend)
        : lerp(current_ycocg, neighborhood_mean, taa_disocclusion_spatial_weight);

    const float4 result = float4(TAALuminanceExpand(YCoCgToRGB(resolved_ycocg)), center.a);

    destination[pixel] = result;
    history_destination[pixel] = result;
    depth_history_destination[pixel] = view_depth;
}
