#pragma once
#include "Backlog.h"
#include "MathUtils.h"
#include "Profiler.h"
#include "RHIDevice.h"
#include "RHIResource.h"
#include "RHISwapchain.h"
#include "Types.h"

#include <atomic>
#include <memory>
#include <mutex>

namespace won::rendering
{
    struct FrameCommandList
    {
        std::unique_ptr<RHICommandAllocator> command_allocator;
        std::unique_ptr<RHICommandList> command_list;
    };

    struct FrameUploadAllocation
    {
        RHIResource* buffer = nullptr;
        void* mapped_data = nullptr;
        Size buffer_offset = 0;
    };

    struct FrameContext
    {
        void BeginFrame()
        {
            if (fence_value > 0)
            {
                profiler::ScopedRangeCPU wait_range("Wait GPU Fence");
                fence->Wait(fence_value);
                fence_value = 0;
            }

            {
                std::scoped_lock lock(frame_upload_mutex);
                frame_upload_offset = 0;
            }
            {
                std::scoped_lock lock(deferred_res_removal_mutex);
                deferred_res_removal.clear();
                deferred_shared_res_removal.clear();
            }

            for (std::atomic<Size>& command_list_count : command_list_counts)
            {
                command_list_count.store(0, std::memory_order_relaxed);
            }
        }

        RHICommandList* BeginCommandList(RHIDevice& device, RHIQueueType queue_type = RHIQueueType::Graphics)
        {
            const Size queue_index = static_cast<Size>(queue_type);
            const Size command_list_index = command_list_counts[queue_index].fetch_add(1, std::memory_order_relaxed);
            Vector<FrameCommandList>& typed_command_lists = command_lists[queue_index];
            {
                std::scoped_lock lock(command_lists_mutex);
                while (command_list_index >= typed_command_lists.size()) // should use while
                {
                    FrameCommandList new_command_list = {};
                    new_command_list.command_allocator = device.CreateCommandAllocator(queue_type);
                    new_command_list.command_list = device.CreateCommandList(queue_type);
                    if (!new_command_list.command_allocator || !new_command_list.command_list)
                    {
                        backlog::Post("failed to create frame command list", backlog::LogLevel::Error);
                        command_list_counts[queue_index].store(typed_command_lists.size(), std::memory_order_relaxed);
                        return nullptr;
                    }
                    typed_command_lists.push_back(std::move(new_command_list));
                }

                FrameCommandList& frame_command_list = typed_command_lists[command_list_index];
                frame_command_list.command_allocator->Reset();
                frame_command_list.command_list->Begin(*frame_command_list.command_allocator);
                return frame_command_list.command_list.get();
            }
        }

        uint64 SubmitCommandLists(RHIContext& context, RHIQueueType queue_type = RHIQueueType::Graphics)
        {
            const Size queue_index = static_cast<Size>(queue_type);
            Vector<FrameCommandList>& typed_command_lists = command_lists[queue_index];

            Vector<RHICommandList*> submitted_command_lists;
            const Size command_list_count = command_list_counts[queue_index].load(std::memory_order_relaxed);
            if (command_list_count == 0)
            {
                return 0;
            }

            submitted_command_lists.reserve(command_list_count);
            for (Size command_list_index = 0; command_list_index < command_list_count; ++command_list_index)
            {
                FrameCommandList& frame_command_list = typed_command_lists[command_list_index];
                RHICommandList* command_list = frame_command_list.command_list.get();
                command_list->End();
                submitted_command_lists.push_back(command_list);
            }

            const uint64 submitted_fence_value = context.Submit(submitted_command_lists, fence.get());
            fence_value = submitted_fence_value;
            return submitted_fence_value;
        }

        bool AllocateFrameUpload(RHIDevice& device, Size size, Size alignment, FrameUploadAllocation& out_allocation)
        {
            std::scoped_lock lock(frame_upload_mutex);
            Size aligned_offset = 0;
            if (!frame_upload_buffer)
            {
                RHIBufferDesc frame_upload_buffer_desc = {};
                frame_upload_buffer_desc.size = math::Align((Size)1024 * 20, alignment);
                frame_upload_buffer_desc.usage = RHIResourceUsage::Upload;
                frame_upload_buffer_desc.bind_flags = RHIBindFlags::ShaderResource | RHIBindFlags::VertexBuffer | RHIBindFlags::IndexBuffer;
                frame_upload_buffer = device.CreateBuffer(frame_upload_buffer_desc);
                if (frame_upload_buffer)
                {
                    frame_upload_buffer->SetName("Frame Upload Buffer");
                }
                else
                {
                    return false;
                }
                frame_upload_offset = 0;
            }

            Size buffer_size = frame_upload_buffer->GetDesc().buffer_desc.size;
            aligned_offset = math::Align(frame_upload_offset, alignment);
            Size required_size = aligned_offset + size;

            if (buffer_size < required_size)
            {
                RHIBufferDesc new_desc = frame_upload_buffer->GetDesc().buffer_desc;
                new_desc.size = math::Align(required_size * 2, alignment);
                RemoveResourceDeferred(std::move(frame_upload_buffer));
                frame_upload_buffer = device.CreateBuffer(new_desc);
                if (frame_upload_buffer)
                {
                    frame_upload_buffer->SetName("Frame Upload Buffer");
                }
                else
                {
                    return false;
                }
            }
            frame_upload_offset = aligned_offset + size;

            void* mapped_data = frame_upload_buffer->GetMappedData();

            out_allocation.buffer = frame_upload_buffer.get();
            out_allocation.mapped_data = static_cast<uint8*>(mapped_data) + aligned_offset;
            out_allocation.buffer_offset = aligned_offset;
            return true;
        }

        void RemoveResourceDeferred(std::unique_ptr<RHIObject> object)
        {
            std::scoped_lock lock(deferred_res_removal_mutex);
            deferred_res_removal.push_back(std::move(object));
        }

        void RemoveSharedResourceDeferred(std::shared_ptr<RHIObject> object)
        {
            std::scoped_lock lock(deferred_res_removal_mutex);
            deferred_shared_res_removal.push_back(std::move(object));
        }

        Vector<FrameCommandList> command_lists[static_cast<Size>(RHIQueueType::Count)];
        std::atomic<Size> command_list_counts[static_cast<Size>(RHIQueueType::Count)] = {};
        std::mutex command_lists_mutex;
        std::mutex frame_upload_mutex;
        std::mutex deferred_res_removal_mutex;
        std::unique_ptr<RHIFence> fence;
        std::unique_ptr<RHIResource> frame_upload_buffer;
        Size frame_upload_offset = 0;
        uint64 fence_value = 0;

        std::vector<std::unique_ptr<RHIObject>> deferred_res_removal;
        std::vector<std::shared_ptr<RHIObject>> deferred_shared_res_removal;
    };
}
