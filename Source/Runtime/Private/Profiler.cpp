#include "Profiler.h"

#include "MathUtils.h"
#include "StringUtils.h"
#include "Timer.h"
#include "FileSystem.h"

#include <algorithm>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace won::profiler
{
    static bool enabled = false;
    static bool enabled_request = false;
    static std::mutex lock;
    static range_id cpu_frame = 0;

    struct Range
    {
        bool in_use = false;
        String name;
        float times[20] = {};
        int avg_counter = 0;
        float time_ms = 0.0f;
        won::utils::Timer cpu_timer;
    };

    static UnorderedMap<range_id, Range> ranges;

    static range_id CombineHash(range_id base, Size value)
    {
        return base ^ (value + 0x9e3779b97f4a7c15ull + (base << 6) + (base >> 2));
    }

    void BeginFrame()
    {
        if (enabled_request != enabled)
        {
            ranges.clear();
            enabled = enabled_request;
        }

        if (!enabled)
        {
            return;
        }

        for (auto& pair : ranges)
        {
            Range& range = pair.second;
            if (!range.in_use)
            {
                continue;
            }

            range.times[range.avg_counter++ % arraysize(range.times)] = range.time_ms;
            if (range.avg_counter > static_cast<int>(arraysize(range.times)))
            {
                float avg_time = 0.0f;
                for (float t : range.times)
                {
                    avg_time += t;
                }
                range.time_ms = avg_time / static_cast<float>(arraysize(range.times));
            }

            range.in_use = false;
        }

        cpu_frame = BeginRangeCPU("CPU Frame");
    }

    void EndFrame()
    {
        if (!enabled)
        {
            return;
        }

        EndRange(cpu_frame);
    }

    range_id BeginRangeCPU(const String& name)
    {
        if (!enabled)
        {
            return 0;
        }

        range_id id = static_cast<range_id>(won::utils::Hash(name));

        std::scoped_lock guard(lock);
        size_t differentiator = 0;
        while (ranges[id].in_use)
        {
            id = CombineHash(id, differentiator++);
        }

        Range& range = ranges[id];
        range.in_use = true;
        range.name = name;
        range.cpu_timer.Reset();

        return id;
    }

    range_id BeginRangeGPU(const String& name)
    {
        return BeginRangeCPU(name);
    }

    void EndRange(range_id id)
    {
        if (!enabled)
        {
            return;
        }

        std::scoped_lock guard(lock);
        auto iter = ranges.find(id);
        if (iter == ranges.end())
        {
            return;
        }

        Range& range = iter->second;
        range.time_ms = static_cast<float>(range.cpu_timer.ElapsedMilliSeconds());
    }

    ScopedRangeCPU::ScopedRangeCPU(const char* name)
    {
        id = BeginRangeCPU(name);
    }

    ScopedRangeCPU::~ScopedRangeCPU()
    {
        EndRange(id);
    }

    ScopedRangeGPU::ScopedRangeGPU(const char* name)
    {
        id = BeginRangeGPU(name);
    }

    ScopedRangeGPU::~ScopedRangeGPU()
    {
        EndRange(id);
    }

    void SetEnabled(bool value)
    {
        enabled_request = value;
    }

    bool IsEnabled()
    {
        return enabled;
    }

    struct Hits
    {
        uint32 num_hits = 0;
        float total_time = 0.0f;
    };

    void GetProfileInfo(String& performance_profile, String& resource_profile)
    {
        if (!enabled || !enabled_request)
        {
            performance_profile.clear();
            resource_profile.clear();
            return;
        }

        UnorderedMap<String, Hits> time_cache;
        std::stringstream ss;
        ss.precision(2);

        for (const auto& pair : ranges)
        {
            const Range& range = pair.second;
            if (!range.in_use)
            {
                continue;
            }

            if (pair.first == cpu_frame)
            {
                continue;
            }

            Hits& hit = time_cache[range.name];
            hit.num_hits++;
            hit.total_time += range.time_ms;
        }

        if (ranges.find(cpu_frame) != ranges.end())
        {
            ss << ranges[cpu_frame].name << ": " << std::fixed << ranges[cpu_frame].time_ms << " ms\n";
        }

        for (auto& pair : time_cache)
        {
            if (pair.second.num_hits > 1)
            {
                ss << "\t" << pair.first << " (" << pair.second.num_hits << "x)"
                   << ": " << std::fixed << pair.second.total_time << " ms\n";
            }
            else if (pair.second.num_hits == 1)
            {
                ss << "\t" << pair.first << ": " << std::fixed << pair.second.total_time << " ms\n";
            }
        }

        performance_profile = ss.str();
        resource_profile.clear();
    }
}
