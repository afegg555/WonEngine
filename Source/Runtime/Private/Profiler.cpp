#include "Profiler.h"

#include "RHIDevice.h"
#include "RHICommandList.h"
#include "RHIContext.h"
#include "RHIQueryHeap.h"
#include "RHIResource.h"
#include "StringUtils.h"
#include "Timer.h"
#include "Platform.h"

#include <array>
#include <mutex>
#include <sstream>

namespace won::profiler
{
    namespace
    {
        enum class RangeType
        {
            CPU,
            GPU,
        };

        struct Range
        {
            bool in_use = false;
            bool has_valid_result = false;
            RangeType type = RangeType::CPU;
            String name;
            float times[20] = {};
            int avg_counter = 0;
            float time_ms = 0.0f;
            won::utils::Timer cpu_timer;
            rendering::RHICommandList* command_list = nullptr;
            std::array<int32, rendering::max_frames_in_flight> query_begin = { -1, -1, -1 };
            std::array<int32, rendering::max_frames_in_flight> query_end = { -1, -1, -1 };
        };

        constexpr uint32 profiler_query_count = 2048;

        bool enabled = false;
        bool enabled_request = false;
        bool gpu_available = false;
        range_id cpu_frame = 0;
        range_id gpu_frame = 0;
        uint32 active_frame_slot = 0;
        uint32 next_query_index = 0;
        double timestamp_per_ms = 0.0;
        rendering::RHIDevice* active_device = nullptr;
        std::mutex lock;
        UnorderedMap<range_id, Range> ranges;
        std::shared_ptr<rendering::RHIQueryHeap> query_heap;
        std::array<std::shared_ptr<rendering::RHIResource>, rendering::max_frames_in_flight> query_readback_buffers = {};

        range_id CombineHash(range_id base, Size value)
        {
            return base ^ (value + 0x9e3779b97f4a7c15ull + (base << 6) + (base >> 2));
        }

        int32 AllocateQuery()
        {
            if (next_query_index >= profiler_query_count)
            {
                return -1;
            }

            return static_cast<int32>(next_query_index++);
        }

        void WriteTimestamp(rendering::RHICommandList& command_list, int32 query_index)
        {
            if (!query_heap || query_index < 0)
            {
                return;
            }

            command_list.EndQuery(*query_heap, static_cast<uint32>(query_index));
        }
    }

    void BeginFrame()
    {
        if (enabled_request != enabled)
        {
            ranges.clear();
            enabled = enabled_request;
            cpu_frame = 0;
            gpu_frame = 0;
            next_query_index = 0;
        }

        if (!enabled)
        {
            return;
        }

        for (auto& pair : ranges)
        {
            Range& range = pair.second;
            if (!range.in_use || range.type != RangeType::CPU)
            {
                continue;
            }

            range.times[range.avg_counter++ % arraysize(range.times)] = range.time_ms;
            if (range.avg_counter > static_cast<int>(arraysize(range.times)))
            {
                float avg = 0.0f;
                for (float time : range.times)
                {
                    avg += time;
                }
                range.time_ms = avg / static_cast<float>(arraysize(range.times));
            }

            range.has_valid_result = true;
            range.in_use = false;
            range.command_list = nullptr;
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

    void BeginFrameGPU(rendering::RHIDevice& device, uint32 frame_slot, rendering::RHICommandList& command_list)
    {
        if (!enabled)
        {
            return;
        }

        active_device = &device;

        auto context = device.GetContext(command_list.GetType());
        if (!context)
        {
            gpu_available = false;
            return;
        }

        const uint64 frequency = context->GetTimestampFrequency();
        if (frequency == 0)
        {
            gpu_available = false;
            return;
        }

        if (!query_heap)
        {
            rendering::RHIQueryHeapDesc query_desc = {};
            query_desc.type = rendering::RHIQueryType::Timestamp;
            query_desc.query_count = profiler_query_count;
            query_heap = device.CreateQueryHeap(query_desc);
            if (!query_heap)
            {
                gpu_available = false;
                return;
            }
            query_heap->SetName("Profiler Query Heap");

            rendering::RHIBufferDesc readback_desc = {};
            readback_desc.usage = rendering::RHIResourceUsage::Readback;
            readback_desc.size = static_cast<Size>(profiler_query_count) * sizeof(uint64);

            for (auto& buffer : query_readback_buffers)
            {
                buffer = device.CreateBuffer(readback_desc);
                if (!buffer)
                {
                    query_heap = nullptr;
                    gpu_available = false;
                    return;
                }
                buffer->SetName("Profiler Query Readback Buffer");
            }
        }

        gpu_available = true;
        timestamp_per_ms = static_cast<double>(frequency) / 1000.0;
        active_frame_slot = frame_slot;
        next_query_index = 0;

        const uint64* query_results = static_cast<const uint64*>(query_readback_buffers[frame_slot]->GetMappedData());
        for (auto& pair : ranges)
        {
            Range& range = pair.second;
            if (!range.in_use || range.type != RangeType::GPU)
            {
                continue;
            }

            const int32 begin_index = range.query_begin[frame_slot];
            const int32 end_index = range.query_end[frame_slot];
            bool has_new_result = false;
            if (query_results && begin_index >= 0 && end_index >= 0 && begin_index < static_cast<int32>(profiler_query_count) && end_index < static_cast<int32>(profiler_query_count))
            {
                const uint64 begin_value = query_results[begin_index];
                const uint64 end_value = query_results[end_index];
                if (end_value >= begin_value)
                {
                    range.time_ms = static_cast<float>((static_cast<double>(end_value - begin_value)) / timestamp_per_ms);
                    has_new_result = true;
                }
            }

            range.query_begin[frame_slot] = -1;
            range.query_end[frame_slot] = -1;
            if (has_new_result)
            {
                range.times[range.avg_counter++ % arraysize(range.times)] = range.time_ms;
                if (range.avg_counter > static_cast<int>(arraysize(range.times)))
                {
                    float avg = 0.0f;
                    for (float time : range.times)
                    {
                        avg += time;
                    }
                    range.time_ms = avg / static_cast<float>(arraysize(range.times));
                }
                range.has_valid_result = true;
            }

            range.in_use = false;
            range.command_list = nullptr;
        }

        command_list.ResetQuery(*query_heap, 0, profiler_query_count);
        gpu_frame = BeginRangeGPU("GPU Frame", command_list);
    }

    void EndFrameGPU(rendering::RHICommandList& command_list)
    {
        if (!enabled || !gpu_available || !query_heap)
        {
            return;
        }

        int32 query_end = -1;
        {
            std::scoped_lock guard(lock);
            auto iter = ranges.find(gpu_frame);
            if (iter != ranges.end())
            {
                query_end = AllocateQuery();
                iter->second.query_end[active_frame_slot] = query_end;
            }
        }
        WriteTimestamp(command_list, query_end);

        if (next_query_index == 0)
        {
            return;
        }

        command_list.ResolveQuery(*query_heap, 0, next_query_index, *query_readback_buffers[active_frame_slot], 0);
    }

    void Shutdown()
    {
        std::scoped_lock guard(lock);
        enabled = false;
        enabled_request = false;
        gpu_available = false;
        cpu_frame = 0;
        gpu_frame = 0;
        active_frame_slot = 0;
        next_query_index = 0;
        timestamp_per_ms = 0.0;
        active_device = nullptr;
        ranges.clear();
        query_heap = nullptr;
        for (auto& buffer : query_readback_buffers)
        {
            buffer = nullptr;
        }
    }

    range_id BeginRangeCPU(const String& name)
    {
        if (!enabled)
        {
            return 0;
        }

        range_id id = static_cast<range_id>(won::utils::Hash(name));

        std::scoped_lock guard(lock);
        Size differentiator = 0;
        while (ranges[id].in_use)
        {
            id = CombineHash(id, differentiator++);
        }

        Range& range = ranges[id];
        range.in_use = true;
        range.type = RangeType::CPU;
        range.name = name;
        range.cpu_timer.Reset();

        return id;
    }

    range_id BeginRangeGPU(const String& name, rendering::RHICommandList& command_list)
    {
        if (!enabled || !gpu_available || !query_heap)
        {
            return 0;
        }

        range_id id = static_cast<range_id>(won::utils::Hash(name));
        int32 query_begin = -1;

        {
            std::scoped_lock guard(lock);
            Size differentiator = 0;
            while (ranges[id].in_use)
            {
                id = CombineHash(id, differentiator++);
            }

            Range& range = ranges[id];
            range.in_use = true;
            range.type = RangeType::GPU;
            range.name = name;
            range.command_list = &command_list;
            query_begin = AllocateQuery();
            range.query_begin[active_frame_slot] = query_begin;
            range.query_end[active_frame_slot] = -1;
        }

        WriteTimestamp(command_list, query_begin);

        return id;
    }

    void EndRange(range_id id)
    {
        if (!enabled || id == 0)
        {
            return;
        }

        rendering::RHICommandList* command_list = nullptr;
        int32 query_end = -1;
        {
            std::scoped_lock guard(lock);
            auto iter = ranges.find(id);
            if (iter == ranges.end())
            {
                return;
            }

            Range& range = iter->second;
            if (range.type == RangeType::CPU)
            {
                range.time_ms = static_cast<float>(range.cpu_timer.ElapsedMilliSeconds());
                return;
            }

            if (!range.command_list)
            {
                return;
            }

            command_list = range.command_list;
            query_end = AllocateQuery();
            range.query_end[active_frame_slot] = query_end;
        }

        WriteTimestamp(*command_list, query_end);
    }

    ScopedRangeCPU::ScopedRangeCPU(const char* name)
    {
        id = BeginRangeCPU(name);
    }

    ScopedRangeCPU::~ScopedRangeCPU()
    {
        EndRange(id);
    }

    ScopedRangeGPU::ScopedRangeGPU(const char* name, rendering::RHICommandList& command_list)
    {
        id = BeginRangeGPU(name, command_list);
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

        UnorderedMap<String, Hits> cpu_time_cache;
        UnorderedMap<String, Hits> gpu_time_cache;
        std::stringstream ss;
        ss.precision(2);

        for (const auto& pair : ranges)
        {
            const Range& range = pair.second;
            if (!range.has_valid_result)
            {
                continue;
            }

            if (range.type == RangeType::CPU)
            {
                if (pair.first == cpu_frame)
                {
                    continue;
                }

                Hits& hit = cpu_time_cache[range.name];
                hit.num_hits++;
                hit.total_time += range.time_ms;
            }
            else
            {
                if (pair.first == gpu_frame)
                {
                    continue;
                }

                Hits& hit = gpu_time_cache[range.name];
                hit.num_hits++;
                hit.total_time += range.time_ms;
            }
        }

        if (ranges.find(cpu_frame) != ranges.end())
        {
            ss << ranges[cpu_frame].name << ": " << std::fixed << ranges[cpu_frame].time_ms << " ms\n";
        }

        for (auto& pair : cpu_time_cache)
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

        ss << "\n";

        if (gpu_available && ranges.find(gpu_frame) != ranges.end())
        {
            ss << ranges[gpu_frame].name << ": " << std::fixed << ranges[gpu_frame].time_ms << " ms\n";
        }
        else
        {
            ss << "GPU Frame: unavailable\n";
        }

        for (auto& pair : gpu_time_cache)
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

        auto format_gb = [](uint64 bytes)
        {
            const double value = static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0);

            std::stringstream bytes_ss;
            bytes_ss.precision(2);
            bytes_ss << std::fixed << value << " GB";
            return bytes_ss.str();
        };

        std::stringstream resources;
        resources.precision(2);
        resources << "Resources\n";

        platform::ProcessMemoryUsage process_memory = {};
        if (platform::GetProcessMemoryUsage(process_memory))
        {
            resources << "RAM\n";
            resources << "\tCommitted: " << format_gb(process_memory.committed_bytes) << "\n";
            resources << "\tWorking Set: " << format_gb(process_memory.working_set_bytes) << "\n";
        }
        else
        {
            resources << "RAM: unavailable\n";
        }

        rendering::RHIMemoryUsage gpu_memory = {};
        if (active_device && active_device->GetMemoryUsage(gpu_memory))
        {
            auto append_gpu_segment = [&](const char* name, const rendering::RHIMemorySegmentUsage& usage)
            {
                resources << "\t" << name << ": " << format_gb(usage.usage_bytes);
                if (usage.budget_bytes > 0)
                {
                    const double budget_percent = static_cast<double>(usage.usage_bytes) / static_cast<double>(usage.budget_bytes) * 100.0;
                    resources << " / " << format_gb(usage.budget_bytes) << " (" << std::fixed << budget_percent << "%)";
                }
                resources << "\n";
            };

            resources << "VRAM\n";
            append_gpu_segment("Local", gpu_memory.local);
            append_gpu_segment("Shared", gpu_memory.non_local);
        }
        else
        {
            resources << "VRAM: unavailable\n";
        }

        resource_profile = resources.str();
    }
}
