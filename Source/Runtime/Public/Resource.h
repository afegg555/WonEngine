#pragma once
#include "Types.h"

namespace won::resource
{
    struct Resource
    {
        String name;

        virtual ~Resource() = default;
        virtual bool IsValid() const = 0;
    };
}
