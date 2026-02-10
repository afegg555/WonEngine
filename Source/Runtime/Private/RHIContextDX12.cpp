#include "RHIContextDX12.h"

#include "Backlog.h"
#include "Platform.h"
#include "RHICommandListDX12.h"

#include "DirectX-Headers/d3d12.h"

namespace won::rendering
{
    RHIContextDX12::RHIContextDX12(RHIQueueType type, ComPtr<ID3D12Device> device_in)
        : queue_type(type)
        , device(std::move(device_in))
    {
        if (!device)
        {
            backlog::Post("DX12 device is not initialized", backlog::LogLevel::Error);
            return;
        }

        D3D12_COMMAND_LIST_TYPE command_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        switch (queue_type)
        {
        case RHIQueueType::Graphics:
            command_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            break;
        case RHIQueueType::Compute:
            command_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            break;
        case RHIQueueType::Copy:
            command_type = D3D12_COMMAND_LIST_TYPE_COPY;
            break;
        }

        D3D12_COMMAND_QUEUE_DESC queue_desc = {};
        queue_desc.Type = command_type;
        queue_desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
        queue_desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
        queue_desc.NodeMask = 0;

        if (FAILED(device->CreateCommandQueue(&queue_desc, IID_PPV_ARGS(&queue))) || !queue)
        {
            backlog::Post("Failed to create command queue", backlog::LogLevel::Error);
            queue.Reset();
        }
    }

    bool RHIContextDX12::IsValid() const
    {
        return device != nullptr && queue != nullptr;
    }

    RHIQueueType RHIContextDX12::GetType() const
    {
        return queue_type;
    }

    uint64 RHIContextDX12::Submit(RHICommandList& command_list, RHIFence* fence)
    {
        auto* dx12_command_list = dynamic_cast<RHICommandListDX12*>(&command_list);
        if (!queue || !dx12_command_list || !dx12_command_list->GetCommandList())
        {
            backlog::Post("Invalid DX12 command list submission", backlog::LogLevel::Error);
            return 0;
        }

        ID3D12CommandList* native_command_lists[] = { dx12_command_list->GetCommandList() };
        queue->ExecuteCommandLists(1, native_command_lists);
        (void)fence;
        return 0;
    }

    void RHIContextDX12::Wait(RHIFence& fence, uint64 value)
    {
        (void)fence;
        (void)value;
    }

    void RHIContextDX12::WaitIdle()
    {
        if (!device || !queue)
        {
            return;
        }

        ComPtr<ID3D12Fence> fence;
        if (FAILED(device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || !fence)
        {
            return;
        }

        HANDLE fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event)
        {
            return;
        }

        const UINT64 fence_value = 1;
        queue->Signal(fence.Get(), fence_value);
        fence->SetEventOnCompletion(fence_value, fence_event);
        WaitForSingleObject(fence_event, INFINITE);
        CloseHandle(fence_event);
    }

    ID3D12CommandQueue* RHIContextDX12::GetQueue() const
    {
        return queue.Get();
    }
}
