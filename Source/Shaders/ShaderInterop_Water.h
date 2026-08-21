#ifndef WON_SHADERINTEROP_WATER_H
#define WON_SHADERINTEROP_WATER_H

#ifndef WON_DISABLE_RENDERER_PUSHCONSTANT
#define WON_DISABLE_RENDERER_PUSHCONSTANT
#endif

#include "ShaderInterop.h"

#define WATER_RIPPLE_GROUP_SIZE 8
#define WATER_RIPPLE_MIN_RESOLUTION 64u
#define WATER_RIPPLE_MAX_RESOLUTION 1024u
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
    uint zone_buffer_descriptor;
    uint zone_index;

    uint height_current_descriptor;
    uint height_previous_descriptor;
    uint wetness_descriptor;
    uint injection_descriptor;
    uint injection_count;
    float step_seconds;

#ifdef __cplusplus
    inline void Init()
    {
        zone_buffer_descriptor = 0;
        zone_index = 0;
        height_current_descriptor = 0;
        height_previous_descriptor = 0;
        wetness_descriptor = 0;
        injection_descriptor = 0;
        injection_count = 0;
        step_seconds = 0.0f;
    }
#endif
};

#ifdef __cplusplus
static_assert(sizeof(WaterPushConstants) == 36, "WaterPushConstants layout mismatch");
static_assert(sizeof(WaterRipplePushConstants) == 32, "WaterRipplePushConstants layout mismatch");
#endif

#endif
