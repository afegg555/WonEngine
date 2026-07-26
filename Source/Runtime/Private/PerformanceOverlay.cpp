#include "PerformanceOverlay.h"

#include "BuiltinFont.h"
#include "Console.h"
#include "DebugDraw.h"
#include "Platform.h"
#include "Profiler.h"
#include "RHIDevice.h"

#include <algorithm>
#include <cstdio>

#ifndef WON_SHIPPING
namespace won::stats
{
    namespace
    {
        console::ConsoleVariable stat_overlay("stat.overlay", 0, "performance overlay: 0=off, 1=fps/memory, 2=full profiler", console::ConsoleVariableFlagNone);

        constexpr float perf_text_scale = 1.0f;
        constexpr uint32 perf_background_color = debugdraw::PackRGBA8(0, 0, 0, 180);
        constexpr uint32 perf_text_color = debugdraw::PackRGBA8(120, 255, 120);

        String FormatGigabytes(uint64 bytes)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.2f GB", static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
            return buffer;
        }
    }

    void PerformanceOverlay::Update(float dt, rendering::RHIDevice* device)
    {
        accumulated_time += dt;
        ++accumulated_frames;
        if (accumulated_time >= 0.5f && accumulated_frames > 0)
        {
            display_frame_ms = (accumulated_time / static_cast<float>(accumulated_frames)) * 1000.0f;
            accumulated_time = 0.0f;
            accumulated_frames = 0;
        }

        const int level = stat_overlay.GetInt();
        if (level <= 0)
        {
            if (overlay_enabled_profiler)
            {
                profiler::SetEnabled(false);
                overlay_enabled_profiler = false;
            }
            return;
        }

        memory_refresh_timer += dt;
        if (memory_refresh_timer >= 0.5f || ram_line.empty())
        {
            memory_refresh_timer = 0.0f;

            platform::ProcessMemoryUsage process_memory = {};
            ram_line = platform::GetProcessMemoryUsage(process_memory) ? ("RAM  " + FormatGigabytes(process_memory.working_set_bytes)) : "RAM  n/a";

            rendering::RHIMemoryUsage gpu_memory = {};
            vram_line = (device && device->GetMemoryUsage(gpu_memory)) ? ("VRAM " + FormatGigabytes(gpu_memory.local.usage_bytes)) : "VRAM n/a";
        }

        if (level >= 2)
        {
            if (!profiler::IsEnabled())
            {
                profiler::SetEnabled(true);
                overlay_enabled_profiler = true;
            }
            String resources;
            profiler::GetProfileInfo(profile_dump, resources);
        }
        else
        {
            if (overlay_enabled_profiler)
            {
                profiler::SetEnabled(false);
                overlay_enabled_profiler = false;
            }
            profile_dump.clear();
        }
    }

    void PerformanceOverlay::Draw(float viewport_width, float viewport_height)
    {
        const int level = stat_overlay.GetInt();
        if (level <= 0)
        {
            return;
        }

        const float glyph_width = static_cast<float>(builtinfont::glyph_width) * perf_text_scale;
        const float line_height = static_cast<float>(builtinfont::glyph_height) * perf_text_scale;
        const float padding = 6.0f;

        char fps_buffer[48];
        const float fps = display_frame_ms > 0.0f ? 1000.0f / display_frame_ms : 0.0f;
        std::snprintf(fps_buffer, sizeof(fps_buffer), "FPS %.0f  %.2f ms", fps, display_frame_ms);

        Vector<String> lines;
        lines.push_back(fps_buffer);
        lines.push_back(ram_line);
        lines.push_back(vram_line);

        if (level >= 2 && !profile_dump.empty())
        {
            Size start = 0;
            while (true)
            {
                const Size newline = profile_dump.find('\n', start);
                const Size end = newline == String::npos ? profile_dump.size() : newline;
                lines.push_back(profile_dump.substr(start, end - start));
                if (newline == String::npos)
                {
                    break;
                }
                start = newline + 1;
            }
        }

        Size longest = 0;
        for (const String& line : lines)
        {
            longest = std::max(longest, line.size());
        }

        const float panel_width = static_cast<float>(longest) * glyph_width + padding * 2.0f;
        const float panel_height = static_cast<float>(lines.size()) * line_height + padding * 2.0f;
        const float margin = 8.0f;
        const bool anchor_right = anchor == OverlayAnchor::TopRight || anchor == OverlayAnchor::BottomRight;
        const bool anchor_bottom = anchor == OverlayAnchor::BottomLeft || anchor == OverlayAnchor::BottomRight;
        const float panel_x = anchor_right ? (viewport_width - panel_width - margin) : margin;
        const float panel_y = anchor_bottom ? (viewport_height - panel_height - margin) : margin;

        debugdraw::Rect2D({ panel_x, panel_y }, { panel_width, panel_height }, perf_background_color);
        float y = panel_y + padding;
        for (const String& line : lines)
        {
            debugdraw::Text2D({ panel_x + padding, y }, line.c_str(), perf_text_color, perf_text_scale);
            y += line_height;
        }
    }
}
#endif
