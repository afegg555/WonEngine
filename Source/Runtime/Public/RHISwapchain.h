#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIResource.h"

namespace won::rendering
{
    inline constexpr uint32 max_frames_in_flight = 3;

    class WONENGINE_API RHISwapchain
    {
    public:
        virtual ~RHISwapchain() = default;
        virtual uint32 GetCurrentBackBufferIndex() const = 0;
        virtual uint32 GetBackBufferCount() const = 0;
        virtual std::shared_ptr<RHIResource> GetCurrentBackBuffer() = 0;
        virtual std::shared_ptr<RHIResource> GetBackBuffer(uint32 index) = 0;
        virtual bool Resize(uint32 width, uint32 height) = 0;
        virtual void SetVSync(bool enabled) = 0;
        virtual bool IsVSyncEnabled() const = 0;
        virtual bool Present() = 0;
    };
}
