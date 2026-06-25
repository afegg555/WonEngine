#pragma once
#include "Types.h"

namespace won::ecs
{
    struct LayerComponent
    {
        uint32 layer_mask = 0xFFFFFFFF;
    };
}
