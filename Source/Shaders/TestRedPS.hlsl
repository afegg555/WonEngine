#include "Common.hlsli"

float4 main() : SV_Target
{
    ShaderMaterial material = GetMaterial(object_push_constants.material_index);
    return material.base_color;
}
