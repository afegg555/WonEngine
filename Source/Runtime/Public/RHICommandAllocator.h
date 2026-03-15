#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIObject.h"

namespace won::rendering
{
    enum class RHIQueueType
    {
        Graphics,
        Compute,
        Copy,

        Count
    };

    class RHICommandAllocator : public RHIObject
    {
    public:
        ~RHICommandAllocator() override = default;

        virtual RHIQueueType GetType() const = 0;
        virtual void Reset() = 0;
    };
}

