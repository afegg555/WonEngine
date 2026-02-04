#pragma once
#include "Types.h"

#include <atomic>
#include <vector>

namespace won::memory
{
    template <typename T>
    class RingBuffer
    {
    public:
        explicit RingBuffer(Size capacity_size)
            : buffer_capacity(capacity_size)
        {
            buffer_data.resize(buffer_capacity);
        }

        T* Allocate(Size count)
        {
            // Atomically increment and get the ticket
            Size ticket = ticket_counter.fetch_add(count, std::memory_order_relaxed);

            return &buffer_data[ticket % buffer_capacity];
        }

    private:
        Size buffer_capacity;
        Vector<T> buffer_data;

        // Single atomic ticket for all threads
        std::atomic<Size> ticket_counter{ 0 };
    };
}