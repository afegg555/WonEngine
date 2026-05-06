#pragma once
#include "Types.h"

namespace won::resource
{
    struct Resource
    {
        virtual ~Resource() = default;
        virtual bool IsValid() const = 0;
    };
}
