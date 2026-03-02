#define OBJECTSHADER_USE_COLOR
#include "ObjectCommon.hlsli"

[earlydepthstencil]
float4 main(PixelInput input) : SV_TARGET
{
    return input.color;
}

