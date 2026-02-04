#pragma once

#include <atomic>

namespace won::utils
{
    class SpinLock
    {
    public:
        void Lock()
        {
            while (flag.test_and_set(std::memory_order_acquire))
            {
            }
        }

        void Unlock()
        {
            flag.clear(std::memory_order_release);
        }

    private:
        std::atomic_flag flag = ATOMIC_FLAG_INIT;
    };
}
