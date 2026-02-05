#include "JobSystem.h"

#include "Platform.h"
#include "Backlog.h"
#include "MathUtils.h"
#include "Timer.h"

#include <algorithm>
#include <cassert>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

namespace won::jobsystem
{
    struct alignas(64) Job
    {
        job_function_type task;
        Context* ctx = nullptr;
        uint32 group_id = 0;
        uint32 group_job_offset = 0;
        uint32 group_job_end = 0;
        uint32 sharedmemory_size = 0;

        uint32 Execute() const
        {
            JobArgs args;
            args.group_id = group_id;

            if (sharedmemory_size > 0)
            {
                static constexpr uint32 alignment = 64;
#if defined(_WIN32)
                void* raw = _alloca(sharedmemory_size + alignment);
#else
                void* raw = alloca(sharedmemory_size + alignment);
#endif
                args.sharedmemory = reinterpret_cast<void*>(
                    won::math::align(reinterpret_cast<uint64>(raw), static_cast<uint64>(alignment)));
            }
            else
            {
                args.sharedmemory = nullptr;
            }

            for (uint32 j = group_job_offset; j < group_job_end; ++j)
            {
                args.job_index = j;
                args.group_index = j - group_job_offset;
                args.is_first_job_in_group = (j == group_job_offset);
                args.is_last_job_in_group = (j == group_job_end - 1);
                task(args);
            }

            return ctx->counter.fetch_sub(1, std::memory_order_relaxed);
        }
    };

    struct JobQueue
    {
        std::deque<Job> items;
        std::mutex locker;
        std::atomic_uint32_t count{ 0 };

        void PushBack(const Job& item)
        {
            std::scoped_lock lock(locker);
            items.push_back(item);
            count.fetch_add(1, std::memory_order_relaxed);
        }

        bool PopFront(Job& item)
        {
            if (count.load(std::memory_order_relaxed) == 0)
            {
                return false;
            }

            std::scoped_lock lock(locker);
            if (items.empty())
            {
                return false;
            }

            item = std::move(items.front());
            items.pop_front();
            count.fetch_sub(1, std::memory_order_relaxed);
            return true;
        }
    };

    struct PriorityResources
    {
        uint32 num_threads = 0;
        std::vector<std::thread> threads;
        std::unique_ptr<JobQueue[]> job_queue_per_thread;
        std::atomic<uint8> next_queue_index{ 0 };
        std::condition_variable sleeping_condition;
        std::mutex sleeping_mutex;
        std::condition_variable waiting_condition;
        std::mutex waiting_mutex;
        uint8 mod_lut[256] = {};

        uint8 ConstrainQueueIndex(uint8 idx) const
        {
            return mod_lut[idx];
        }

        uint8 GetNextQueueIndex()
        {
            uint8 idx = next_queue_index.fetch_add(1, std::memory_order_relaxed);
            return ConstrainQueueIndex(idx);
        }

        JobQueue& NextQueue()
        {
            return job_queue_per_thread[GetNextQueueIndex()];
        }

        void Work(uint32 starting_queue)
        {
            Job job;
            for (uint32 i = 0; i < num_threads; ++i)
            {
                JobQueue& job_queue = job_queue_per_thread[ConstrainQueueIndex(static_cast<uint8>(starting_queue))];
                while (job_queue.PopFront(job))
                {
                    uint32 progress_before = job.Execute();
                    if (progress_before == 1)
                    {
                        std::unique_lock<std::mutex> lock(waiting_mutex);
                        waiting_condition.notify_all();
                    }
                }
                ++starting_queue;
            }
        }
    };

    struct InternalState
    {
        uint32 num_cores = 0;
        PriorityResources resources[int(Priority::Count)];
        std::atomic_bool alive{ true };

        void ShutDown()
        {
            if (IsShuttingDown())
            {
                return;
            }

            alive.store(false);

            for (auto& res : resources)
            {
                res.sleeping_condition.notify_all();
            }

            for (auto& res : resources)
            {
                for (auto& thread : res.threads)
                {
                    thread.join();
                }
            }

            for (auto& res : resources)
            {
                res.job_queue_per_thread.reset();
                res.threads.clear();
                res.num_threads = 0;
            }

            num_cores = 0;
        }

        ~InternalState()
        {
            ShutDown();
        }
    };

    static InternalState internal_state;

    void Initialize(uint32 max_thread_count)
    {
        if (internal_state.num_cores > 0)
        {
            return;
        }

        max_thread_count = won::math::clamp(max_thread_count, 1u, static_cast<uint32>(arraysize(PriorityResources::mod_lut)));

        won::utils::Timer timer;

        uint32 hardware_threads = std::thread::hardware_concurrency();
        internal_state.num_cores = std::max(1u, hardware_threads);

        for (int prio = 0; prio < int(Priority::Count); ++prio)
        {
            Priority priority = static_cast<Priority>(prio);
            PriorityResources& res = internal_state.resources[prio];

            switch (priority)
            {
            case Priority::High:
                res.num_threads = internal_state.num_cores > 1 ? internal_state.num_cores - 1 : 1;
                break;
            case Priority::Low:
                res.num_threads = internal_state.num_cores > 2 ? internal_state.num_cores - 2 : 1;
                break;
            case Priority::Streaming:
                res.num_threads = 1;
                break;
            default:
                assert(false);
                res.num_threads = 1;
                break;
            }

            res.num_threads = won::math::clamp(res.num_threads, 1u, max_thread_count);
            res.job_queue_per_thread.reset(new JobQueue[res.num_threads]);
            res.threads.reserve(res.num_threads);

            for (uint32 i = 0; i < arraysize(res.mod_lut); ++i)
            {
                res.mod_lut[i] = static_cast<uint8>(i % res.num_threads);
            }

            for (uint32 thread_id = 0; thread_id < res.num_threads; ++thread_id)
            {
                std::thread& worker = res.threads.emplace_back([thread_id, priority, &res] {
                    while (internal_state.alive.load(std::memory_order_relaxed))
                    {
                        res.Work(thread_id);
                        std::unique_lock<std::mutex> lock(res.sleeping_mutex);
                        res.sleeping_condition.wait(lock);
                    }
                });

#if defined(_WIN32)
                HANDLE handle = worker.native_handle();
                uint32 core = thread_id + 1;
                if (priority == Priority::Streaming)
                {
                    core = internal_state.num_cores - 1 - thread_id;
                }

                DWORD_PTR affinity_mask = 1ull << core;
                DWORD_PTR affinity_result = SetThreadAffinityMask(handle, affinity_mask);
                assert(affinity_result > 0);

                if (priority == Priority::High)
                {
                    BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_NORMAL);
                    assert(priority_result != 0);

                    WString thread_name = L"won::job_" + std::to_wstring(thread_id);
                    HRESULT hr = SetThreadDescription(handle, thread_name.c_str());
                    assert(SUCCEEDED(hr));
                }
                else if (priority == Priority::Low)
                {
                    BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_LOWEST);
                    assert(priority_result != 0);

                    WString thread_name = L"won::job_lo_" + std::to_wstring(thread_id);
                    HRESULT hr = SetThreadDescription(handle, thread_name.c_str());
                    assert(SUCCEEDED(hr));
                }
                else if (priority == Priority::Streaming)
                {
                    BOOL priority_result = SetThreadPriority(handle, THREAD_PRIORITY_BELOW_NORMAL);
                    assert(priority_result != 0);

                    WString thread_name = L"won::job_st_" + std::to_wstring(thread_id);
                    HRESULT hr = SetThreadDescription(handle, thread_name.c_str());
                    assert(SUCCEEDED(hr));
                }
#endif
            }
        }

        String log = "JobSystem initialized with " + std::to_string(internal_state.num_cores) + " cores in "
            + std::to_string(timer.ElapsedMilliSeconds()) + " ms\n\tHigh priority threads : "
            + std::to_string(GetThreadCount(Priority::High)) + "\n\tLow priority threads : "
            + std::to_string(GetThreadCount(Priority::Low)) + "\n\tStreaming priority threads : "
            + std::to_string(GetThreadCount(Priority::Streaming));

        won::backlog::Post(log.c_str(), won::backlog::LogLevel::Default);
    }

    void ShutDown()
    {
        internal_state.ShutDown();
    }

    bool IsShuttingDown()
    {
        return internal_state.alive.load(std::memory_order_relaxed) == false;
    }

    uint32 GetThreadCount(Priority priority)
    {
        return internal_state.resources[int(priority)].num_threads;
    }

    void Execute(Context& ctx, const job_function_type& task)
    {
        PriorityResources& res = internal_state.resources[int(ctx.priority)];

        ctx.counter.fetch_add(1, std::memory_order_relaxed);

        Job job;
        job.ctx = &ctx;
        job.task = task;
        job.group_id = 0;
        job.group_job_offset = 0;
        job.group_job_end = 1;
        job.sharedmemory_size = 0;

        if (res.num_threads < 1)
        {
            job.Execute();
            return;
        }

        res.NextQueue().PushBack(job);
        res.sleeping_condition.notify_one();
    }

    void Dispatch(Context& ctx, uint32 job_count, uint32 group_size, const job_function_type& task, Size sharedmemory_size)
    {
        if (job_count == 0 || group_size == 0)
        {
            return;
        }

        PriorityResources& res = internal_state.resources[int(ctx.priority)];
        uint32 group_count = DispatchGroupCount(job_count, group_size);

        ctx.counter.fetch_add(group_count, std::memory_order_relaxed);

        Job job;
        job.ctx = &ctx;
        job.task = task;
        job.sharedmemory_size = static_cast<uint32>(sharedmemory_size);

        for (uint32 group_id = 0; group_id < group_count; ++group_id)
        {
            job.group_id = group_id;
            job.group_job_offset = group_id * group_size;
            job.group_job_end = std::min(job.group_job_offset + group_size, job_count);

            if (res.num_threads < 1)
            {
                job.Execute();
            }
            else
            {
                res.NextQueue().PushBack(job);
            }
        }

        if (res.num_threads > 1)
        {
            res.sleeping_condition.notify_all();
        }
    }

    uint32 DispatchGroupCount(uint32 job_count, uint32 group_size)
    {
        return (job_count + group_size - 1) / group_size;
    }

    bool IsBusy(const Context& ctx)
    {
        return ctx.counter.load(std::memory_order_relaxed) > 0;
    }

    void Wait(const Context& ctx)
    {
        if (IsBusy(ctx))
        {
            PriorityResources& res = internal_state.resources[int(ctx.priority)];

            res.sleeping_condition.notify_all();
            res.Work(res.GetNextQueueIndex());

            while (IsBusy(ctx))
            {
                std::unique_lock<std::mutex> lock(res.waiting_mutex);
                if (IsBusy(ctx))
                {
                    res.waiting_condition.wait(lock, [&ctx] { return !IsBusy(ctx); });
                }
            }
        }
    }

    uint32 GetRemainingJobCount(const Context& ctx)
    {
        return ctx.counter.load(std::memory_order_relaxed);
    }
}
