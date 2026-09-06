float3 SrgbToLinear(float3 value)
{
    const float3 color = max(value, 0.0f);
    const float3 low = color / 12.92f;
    const float3 high = pow((color + 0.055f) / 1.055f, 2.4f);
    return lerp(low, high, step(0.04045f, color));
}

float3 LinearToSrgb(float3 value)
{
    const float3 color = max(value, 0.0f);
    const float3 low = color * 12.92f;
    const float3 high = 1.055f * pow(color, 1.0f / 2.4f) - 0.055f;
    return lerp(low, high, step(0.0031308f, color));
}
