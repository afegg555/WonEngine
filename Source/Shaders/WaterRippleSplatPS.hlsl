#include "WaterCommon.hlsli"

float4 main(WaterRippleSplatOutput input) : SV_Target0
{
    const float falloff = saturate(1.0f - length(input.unit));
    return float4(input.strength * falloff * falloff, 0.0f, 0.0f, 1.0f); // additive blend scales by alpha
}
