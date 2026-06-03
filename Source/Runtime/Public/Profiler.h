#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <string>

namespace won::rendering
{
    class RHIDevice;
    class RHICommandList;
}

namespace won::profiler
{
    using range_id = Size;

    WONENGINE_API void BeginFrame();
    WONENGINE_API void EndFrame();
    WONENGINE_API void BeginFrameGPU(rendering::RHIDevice& device, uint32 frame_slot, rendering::RHICommandList& command_list);
    WONENGINE_API void EndFrameGPU(rendering::RHICommandList& command_list);
    WONENGINE_API void Shutdown();

    WONENGINE_API range_id BeginRangeCPU(const String& name);
    WONENGINE_API range_id BeginRangeGPU(const String& name, rendering::RHICommandList& command_list);
    WONENGINE_API void EndRange(range_id id);

    struct WONENGINE_API ScopedRangeCPU
    {
        range_id id = 0;
        explicit ScopedRangeCPU(const char* name);
        ~ScopedRangeCPU();
    };

    struct WONENGINE_API ScopedRangeGPU
    {
        range_id id = 0;
        ScopedRangeGPU(const char* name, rendering::RHICommandList& command_list);
        ~ScopedRangeGPU();
    };

    WONENGINE_API void SetEnabled(bool enabled);
    WONENGINE_API bool IsEnabled();
    WONENGINE_API void GetProfileInfo(String& performance_profile, String& resource_profile);
}
