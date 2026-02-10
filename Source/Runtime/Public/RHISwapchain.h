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
        virtual std::shared_ptr<RHIResource> GetCurrentBackBuffer() = 0;
        virtual bool Present() = 0;
    };
}
