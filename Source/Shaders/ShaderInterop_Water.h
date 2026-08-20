#ifndef WON_SHADERINTEROP_WATER_H
#define WON_SHADERINTEROP_WATER_H

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
#define WON_DISABLE_RENDERER_PUSHCONSTANT
#endif

#include "ShaderInterop.h"

#define WATER_RIPPLE_RESOLUTION 256
#define WATER_RIPPLE_GROUP_SIZE 8
#define WATER_SIMULATION_WINDOW_EXTENT 64.0f
#define WATER_SIMULATION_TEXEL_SIZE (WATER_SIMULATION_WINDOW_EXTENT / (float)WATER_RIPPLE_RESOLUTION)
#define WATER_INFO_NO_BODY -1.0f
#define WATER_INFO_HEIGHT_RANGE 4096.0f
#define WATER_TILE_NEIGHBOR_NEG_X 1
#define WATER_TILE_NEIGHBOR_POS_X 2
#define WATER_TILE_NEIGHBOR_NEG_Z 4
#define WATER_TILE_NEIGHBOR_POS_Z 8

struct WaterInfoPushConstants
{
    uint zone_buffer_descriptor;
    uint body_buffer_descriptor;
    uint zone_index;
    uint first_body;
    float max_vertex_spacing;
    uint writes_body_index;

#ifdef __cplusplus
    inline void Init()
    {
        zone_buffer_descriptor = 0;
        body_buffer_descriptor = 0;
        zone_index = 0;
        first_body = 0;
        max_vertex_spacing = 0.0f;
        writes_body_index = 0;
    }
#endif
};

struct WaterPushConstants
{
    uint depth_descriptor;
    uint scene_color_descriptor;
    uint body_buffer_descriptor;
    uint zone_buffer_descriptor;
    uint zone_index;
    uint info_texture_descriptor;
    uint tile_buffer_descriptor;
    uint first_tile;
    uint tile_resolution;

#ifdef __cplusplus
    inline void Init()
    {
        depth_descriptor = 0;
        scene_color_descriptor = 0;
        body_buffer_descriptor = 0;
        zone_buffer_descriptor = 0;
        zone_index = 0;
        info_texture_descriptor = 0;
        tile_buffer_descriptor = 0;
        first_tile = 0;
        tile_resolution = 0;
    }
#endif
};

struct WaterRipplePushConstants
{
    int2 window_grid_min;
    int2 window_grid_shift;

    uint height_current_descriptor;
    uint height_previous_descriptor;
    uint wetness_descriptor;
    uint injection_descriptor;
    uint injection_count;

#ifdef __cplusplus
    inline void Init()
    {
        window_grid_min = { 0, 0 };
        window_grid_shift = { 0, 0 };
        height_current_descriptor = 0;
        height_previous_descriptor = 0;
        wetness_descriptor = 0;
        injection_descriptor = 0;
        injection_count = 0;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(WaterPushConstants) == 36, "WaterPushConstants layout mismatch");
#endif

#endif
