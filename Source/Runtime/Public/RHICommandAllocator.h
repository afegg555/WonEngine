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
        Copy
    };

    class WONENGINE_API RHICommandAllocator : public RHIObject
    {
    public:
        virtual ~RHICommandAllocator() = default;

        virtual RHIQueueType GetType() const = 0;
        virtual void Reset() = 0;
    };
}

