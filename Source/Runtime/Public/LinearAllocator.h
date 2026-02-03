#pragma once
#include "Allocator.h"

#include <new>

namespace won::memory
{
    class LinearAllocator : public Allocator
    {
    public:
        LinearAllocator(Size total_size)
            : total_size(total_size), offset(0)
        {
            data = ::operator new(total_size); // Allocate raw memory without calling constructors
        }

        virtual ~LinearAllocator() override
        {
            ::operator delete(data);
        }

        LinearAllocator(const LinearAllocator&) = delete;
        LinearAllocator& operator=(const LinearAllocator&) = delete;

        virtual void* Allocate(Size size, Size alignment) override
        {
            uintptr_t current_addr = reinterpret_cast<uintptr_t>(data) + offset;
            uintptr_t padding = 0;

            if (alignment != 0 && (current_addr % alignment != 0))
            {
                padding = alignment - (current_addr % alignment);
            }

            if (offset + padding + size > total_size)
            {
                return nullptr;
            }

            offset += padding;
            void* allocated_ptr = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(data) + offset);
            offset += size;

            return allocated_ptr;
        }

        virtual void Deallocate(void* ptr, Size size, Size alignment) override
        {
            // Linear Allocator does not support individual deallocation
        }

        void Reset()
        {
            offset = 0;
        }

    private:
        void* data = nullptr;
        Size  total_size;
        Size  offset;
    };
}
