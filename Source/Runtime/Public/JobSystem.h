#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <atomic>
#include <functional>

namespace won::jobsystem
{
    WONENGINE_API void Initialize(uint32 max_thread_count = ~0u);
    WONENGINE_API void ShutDown();

    // Returns true if the job system is shutting down
    // Long-running jobs should check this and exit themselves if true
    WONENGINE_API bool IsShuttingDown();

    struct JobArgs
    {
        uint32 job_index = 0;
        uint32 group_id = 0;
        uint32 group_index = 0;
        bool is_first_job_in_group = false;
        bool is_last_job_in_group = false;
        void* sharedmemory = nullptr;
    };

    using job_function_type = std::function<void(JobArgs)>;

    enum class Priority
    {
        High,
        Low,
        Streaming,
        Count
    };

    struct Context
    {
        std::atomic<uint32> counter{ 0 };
        Priority priority = Priority::High;
    };

    WONENGINE_API uint32 GetThreadCount(Priority priority = Priority::High);
    WONENGINE_API void Execute(Context& ctx, const job_function_type& task);
    WONENGINE_API void Dispatch(Context& ctx, uint32 job_count, uint32 group_size, const job_function_type& task, Size sharedmemory_size = 0);
    WONENGINE_API uint32 DispatchGroupCount(uint32 job_count, uint32 group_size);
    WONENGINE_API bool IsBusy(const Context& ctx);
    WONENGINE_API void Wait(const Context& ctx);
    WONENGINE_API uint32 GetRemainingJobCount(const Context& ctx);
}
