#include "WaterCommon.hlsli"

float2 main(WaterInfoOutput input) : SV_Target0
{
    return float2(input.height, (float) input.body_index); // depth is height, heigher body index wins
}
