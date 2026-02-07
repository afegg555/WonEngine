#pragma once
#include "RHICommandList.h"

namespace won::rendering
{
    class WONENGINE_API RHIFence
    {
    public:
        virtual ~RHIFence() = default;

        virtual uint64 GetCompletedValue() const = 0;
        virtual void Wait(uint64 value) = 0;
    };

    class RHIContext
    {
    public:
        virtual ~RHIContext() = default;

        virtual RHIQueueType GetType() const = 0;
        virtual uint64 Submit(RHICommandList& command_list, RHIFence* fence = nullptr) = 0;
        virtual void Wait(RHIFence& fence, uint64 value) = 0;
        virtual void WaitIdle() = 0;
    };
}
