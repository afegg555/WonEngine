#pragma once
#include "MathTypes.h"
#include "Types.h"

namespace won::ecs
{
    struct LayoutComponent
    {
		// if this component is attached to an entity, it will automatically layout its children based on the specified layout type and parameters

        enum class Type : uint32
        {
            Horizontal,
            Vertical,
        };

        enum class CrossAlign : uint32
        {
            Start,
            Center,
            End,
            Stretch,
        };

        Type type = Type::Horizontal;
        float2 padding_min = { 0.0f, 0.0f };
        float2 padding_max = { 0.0f, 0.0f };
        float spacing = 0.0f;
        CrossAlign cross_align = CrossAlign::Start;
        bool reverse = false;
    };
}
