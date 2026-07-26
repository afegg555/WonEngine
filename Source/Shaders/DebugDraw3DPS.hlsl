#define WON_DISABLE_RENDERER_PUSHCONSTANT
#include "Common.hlsli"

struct PixelInput
{
    float4 pos : SV_Position;
    float4 color : COLOR0;
};

float4 main(PixelInput input) : SV_Target
{
    return input.color;
}
