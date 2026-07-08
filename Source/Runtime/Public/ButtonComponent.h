#pragma once
#include "Types.h"

namespace won::ecs
{
    struct ButtonComponent
    {
        bool enabled = true;

        void SetEnabled(bool value) { enabled = value; }
        bool IsEnabled() const { return enabled; }
    };
}
