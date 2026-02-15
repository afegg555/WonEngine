#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIResource.h"

namespace won::rendering
{
    class WONENGINE_API RHISwapchain
    {
    public:
        virtual ~RHISwapchain() = default;
        virtual uint32 GetCurrentBackBufferIndex() const = 0;
        virtual uint32 GetBackBufferCount() const = 0;
        virtual std::shared_ptr<RHIResource> GetCurrentBackBuffer() = 0;
        virtual bool Present() = 0;
    };
}
