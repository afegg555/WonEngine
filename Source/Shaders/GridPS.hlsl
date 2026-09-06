#include "Common.hlsli"
#include "ColorSpace.hlsli"

struct PixelInput
{
    float4 position : SV_Position;
    float3 near_point : NEARPOINT;
    float3 far_point : FARPOINT;
};

struct PixelOutput
{
    float4 color : SV_Target;
    float depth : SV_Depth;
};

static const float4 grid_color = float4(0.32f, 0.32f, 0.34f, 0.55f);
static const float4 grid_axis_x_color = float4(0.82f, 0.24f, 0.24f, 0.85f);
static const float4 grid_axis_z_color = float4(0.24f, 0.42f, 0.88f, 0.85f);

float GridLine(float2 world_xz, float spacing)
{
    float2 cell = world_xz / spacing;
    float2 width = max(fwidth(cell), 0.0001f);
    float2 distance_to_line = abs(frac(cell - 0.5f) - 0.5f) / width;
    return 1.0f - saturate(min(distance_to_line.x, distance_to_line.y));
}

float AxisLine(float value)
{
    float width = max(fwidth(value), 0.0001f);
    return 1.0f - saturate(abs(value) / width);
}

PixelOutput main(PixelInput input)
{
    float ray_y = input.far_point.y - input.near_point.y;
    if (abs(ray_y) < 0.00001f)
    {
        discard;
    }

    float plane_t = -input.near_point.y / ray_y;
    if (plane_t <= 0.0f)
    {
        discard;
    }

    float3 world_position = input.near_point + plane_t * (input.far_point - input.near_point);
    float4 clip_position = mul(GetCamera().view_projection, float4(world_position, 1.0f));
    float depth = clip_position.z / max(abs(clip_position.w), 0.000001f);
    if (depth < 0.0f || depth > 1.0f)
    {
        discard;
    }

    float minor_line = GridLine(world_position.xz, 1.0f);
    float major_line = GridLine(world_position.xz, 10.0f);
    float x_axis = AxisLine(world_position.z);
    float z_axis = AxisLine(world_position.x);

    float3 color = grid_color.rgb;
    float alpha = minor_line * grid_color.a;
    if (major_line > minor_line)
    {
        color = saturate(grid_color.rgb + 0.10f);
        alpha = max(alpha, major_line * saturate(grid_color.a + 0.12f));
    }
    if (x_axis > max(minor_line, major_line))
    {
        color = grid_axis_x_color.rgb;
        alpha = max(alpha, x_axis * grid_axis_x_color.a);
    }
    if (z_axis > max(max(minor_line, major_line), x_axis))
    {
        color = grid_axis_z_color.rgb;
        alpha = max(alpha, z_axis * grid_axis_z_color.a);
    }

    float camera_distance = length(world_position - GetCamera().position);
    alpha *= 1.0f - smoothstep(80.0f, 160.0f, camera_distance);
    if (alpha <= 0.001f)
    {
        discard;
    }

    PixelOutput output;
    output.color = float4(SrgbToLinear(color), saturate(alpha));
    output.depth = saturate(depth);
    return output;
}
