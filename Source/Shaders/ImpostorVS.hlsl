#include "ImpostorCommon.hlsli"

ImpostorPixel main(uint vertex_id : SV_VertexID, uint instance_id : SV_InstanceID)
{
    ImpostorPixel output;

    const ShaderFoliageImpostor impostor = GetFoliageImpostor(impostorpush.impostor_index);
    const uint foliage_instance_buffer = DescriptorIndex(GetScene().foliage_instance_buffer);
    const uint foliage_index_buffer = DescriptorIndex(GetView().foliage_instance_index_buffer);
    const uint actual_instance = bindless_buffers_uint[foliage_index_buffer][impostorpush.instance_offset + instance_id];
    const uint base = actual_instance * 2;
    const float4 position_scale = bindless_buffers_float4[foliage_instance_buffer][base + 0];
    const float4 rotation = bindless_buffers_float4[foliage_instance_buffer][base + 1];
    const float scale = position_scale.w;
    const float3x3 rotation_matrix = ImpostorRotationMatrix(rotation);

    const float3 world_center = position_scale.xyz + mul(rotation_matrix, impostor.center) * scale;
    const float world_radius = impostor.radius * scale;

    ShaderCamera camera = GetCamera();
    const float3 to_camera = normalize(camera.position - world_center);
    const float3 world_up = float3(0.0f, 1.0f, 0.0f);
    const float3 billboard_right = normalize(cross(to_camera, world_up));
    const float3 billboard_up = normalize(cross(billboard_right, to_camera));

    const float2 corner = impostor_quad_corners[vertex_id];
    const float2 offset = (corner * 2.0f - 1.0f) * world_radius;
    const float3 world_position = world_center + billboard_right * offset.x + billboard_up * offset.y;

    output.pos = mul(camera.view_projection, float4(world_position, 1.0f));
    output.worldpos = world_position;
    output.current_clip_position = output.pos;
    output.previous_clip_position = mul(camera.previous_view_projection, float4(world_position, 1.0f));
    output.previous_view_depth = dot(world_position - camera.previous_position, camera.previous_forward);
    output.quad_uv = float2(corner.x, 1.0f - corner.y);
    output.rotation = rotation;

    const float3 view_dir_local = mul(to_camera, rotation_matrix);
    const float3 oct_dir = float3(view_dir_local.x, view_dir_local.z, view_dir_local.y);
    const float2 encoded = EncodeHemiOctahedralDirection(oct_dir);
    const float grid = (float)impostor.grid_size;
    const float2 cell = clamp(floor(encoded * grid), 0.0f, grid - 1.0f);
    output.cell_base = cell / grid;

    return output;
}
