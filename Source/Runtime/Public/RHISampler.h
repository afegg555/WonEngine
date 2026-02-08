#pragma once
#include "RHIObject.h"

namespace won::rendering
{
    enum class RHIFilter
    {
        Nearest,
        Linear
    };

    enum class RHIAddressMode
    {
        Wrap,
        Clamp,
        Mirror
    };

    struct RHISamplerDesc
    {
        RHIFilter min_filter = RHIFilter::Linear;
        RHIFilter mag_filter = RHIFilter::Linear;
        RHIFilter mip_filter = RHIFilter::Linear;
        RHIAddressMode address_u = RHIAddressMode::Wrap;
        RHIAddressMode address_v = RHIAddressMode::Wrap;
        RHIAddressMode address_w = RHIAddressMode::Wrap;
        float mip_lod_bias = 0.0f;
        float min_lod = 0.0f;
        float max_lod = 1000.0f;
    };

    class WONENGINE_API RHISampler : public RHIObject
    {
    public:
        ~RHISampler() override = default;
    };
}

