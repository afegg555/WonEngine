#pragma once
#include "Types.h"

namespace won::memory
{
    class Allocator
    {
    public:
        virtual ~Allocator() = default;

        virtual void* Allocate(Size size, Size alignment) = 0;
        virtual void Deallocate(void* ptr, Size size, Size alignment) = 0;
    };
}
