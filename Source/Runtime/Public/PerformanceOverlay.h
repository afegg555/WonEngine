#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::rendering
{
    class RHIDevice;
}

namespace won::stats
{
    class WONENGINE_API PerformanceOverlay
    {
    public:
        void Update(float dt, rendering::RHIDevice* device);
        void Draw(float viewport_width, float viewport_height);

    private:
        float accumulated_time = 0.0f;
        int accumulated_frames = 0;
        float display_frame_ms = 0.0f;
        float memory_refresh_timer = 0.0f;
        String ram_line;
        String vram_line;
        String profile_dump;
        bool overlay_enabled_profiler = false;
    };
}
