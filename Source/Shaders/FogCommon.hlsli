#ifndef FOG_COMMON
#define FOG_COMMON

#include "Common.hlsli"

inline half4 GetFog(float distance, float fog_start, float fog_end)
{
    half3 fog_color = half3(0.6f, 0.6f, 0.6f);
    distance = clamp(distance, fog_start, fog_end);
    
    half fog = saturate((distance - fog_start) / (fog_end - fog_start)); // 0~1
    return half4(fog_color, fog);
}

#endif // FOG_COMMON