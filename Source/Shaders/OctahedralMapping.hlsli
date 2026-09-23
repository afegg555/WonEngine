#ifndef WON_OCTAHEDRAL_MAPPING_HLSLI
#define WON_OCTAHEDRAL_MAPPING_HLSLI

inline float2 EncodeOctahedralDirection(float3 direction)
{
    direction /= max(abs(direction.x) + abs(direction.y) + abs(direction.z), 0.0001f);
    if (direction.z < 0.0f)
    {
        float2 direction_sign = float2(direction.x >= 0.0f ? 1.0f : -1.0f, direction.y >= 0.0f ? 1.0f : -1.0f);
        direction.xy = (1.0f - abs(direction.yx)) * direction_sign;
    }
    return direction.xy * 0.5f + 0.5f;
}

inline float3 DecodeOctahedralDirection(float2 encoded)
{
    float3 direction = float3(encoded.x, encoded.y, 1.0f - abs(encoded.x) - abs(encoded.y));
    if (direction.z < 0.0f)
    {
        float2 direction_sign = float2(direction.x >= 0.0f ? 1.0f : -1.0f, direction.y >= 0.0f ? 1.0f : -1.0f);
        direction.xy = (1.0f - abs(direction.yx)) * direction_sign;
    }
    return normalize(direction);
}

inline float2 EncodeHemiOctahedralDirection(float3 direction)
{
    direction /= max(abs(direction.x) + abs(direction.y) + abs(direction.z), 0.0001f);
    return float2((direction.x + direction.y) * 0.5f + 0.5f, (direction.x - direction.y) * 0.5f + 0.5f);
}

inline float3 DecodeHemiOctahedralDirection(float2 encoded)
{
    float x = (encoded.x + encoded.y) * 0.5f;
    float y = (encoded.x - encoded.y) * 0.5f;
    float z = 1.0f - abs(x) - abs(y);
    return normalize(float3(x, y, z));
}

#endif // WON_OCTAHEDRAL_MAPPING_HLSLI
