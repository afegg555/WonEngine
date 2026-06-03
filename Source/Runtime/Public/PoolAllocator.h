#pragma once
#include "Types.h"

#include <new>

namespace won::memory
{
    class PoolAllocator
    {
    public:
        PoolAllocator(Size block_size_in, Size block_alignment_in, Size blocks_per_page_in = 64)
            : block_size(block_size_in)
            , block_alignment(block_alignment_in)
            , blocks_per_page(blocks_per_page_in)
        {
            if (block_size < sizeof(FreeBlock))
            {
                block_size = sizeof(FreeBlock); // empty blocks store free-list nodes, so each block must be large enough to store a pointer
            }
            if (block_alignment < alignof(FreeBlock))
            {
                block_alignment = alignof(FreeBlock);
            }
            if (blocks_per_page == 0)
            {
                blocks_per_page = 1;
            }

            stride = ((block_size + block_alignment - 1) / block_alignment) * block_alignment; // ex) block_size = 36, block_alignment = 16 -> stride = 48
        }

        ~PoolAllocator()
        {
            for (void* page : pages)
            {
                ::operator delete(page, std::align_val_t(block_alignment));
            }
        }

        PoolAllocator(const PoolAllocator&) = delete;
        PoolAllocator& operator=(const PoolAllocator&) = delete;

        void* Allocate()
        {
            if (!free_list)
            {
                AddPage();
            }

            FreeBlock* block = free_list;
            free_list = free_list->next;
            return block;
        }

        void Deallocate(void* ptr)
        {
            if (!ptr)
            {
                return;
            }

            FreeBlock* block = static_cast<FreeBlock*>(ptr);
            block->next = free_list;
            free_list = block;
        }

    private:
        struct FreeBlock
        {
            FreeBlock* next = nullptr;
        };

        void AddPage()
        {
            const Size page_size = stride * blocks_per_page;
            void* page = ::operator new(page_size, std::align_val_t(block_alignment));
            pages.push_back(page);

            uint8* bytes = static_cast<uint8*>(page);
            for (Size i = blocks_per_page; i > 0; --i)
            {
                FreeBlock* block = reinterpret_cast<FreeBlock*>(bytes + (i - 1) * stride);
                block->next = free_list;
                free_list = block;
            }
        }

        Vector<void*> pages;
        FreeBlock* free_list = nullptr;
        Size block_size = 0;
        Size block_alignment = 0;
        Size stride = 0;
        Size blocks_per_page = 0;
    };
}
