#pragma once
#include "Types.h"

namespace won::rendering
{
    enum class ViewResizePolicy
    {
        Manual,
        MatchWindow
    };

    enum class AntiAliasingMode : uint8
    {
        None,
        FXAA,
        // TAA, MSAA: future
    };

    enum class TonemapMode : uint8
    {
        Reinhard,
        ACES
    };
}
