#include "RHIFenceDX12.h"

#include "Backlog.h"

namespace won::rendering
{
    RHIFenceDX12::RHIFenceDX12(ComPtr<ID3D12Device> device_in, uint64 initial_value)
        : next_value(initial_value)
    {
        if (!device_in)
        {
            backlog::Post("RHIFenceDX12 requires valid DX12 device", backlog::LogLevel::Error);
            return;
        }

        if (FAILED(device_in->CreateFence(initial_value, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence))) || !fence)
        {
            backlog::Post("Failed to create DX12 fence", backlog::LogLevel::Error);
            return;
        }

        fence_event = CreateEventA(nullptr, FALSE, FALSE, nullptr);
        if (!fence_event)
        {
            backlog::Post("Failed to create fence event", backlog::LogLevel::Error);
            fence.Reset();
            return;
        }
    }

    RHIFenceDX12::~RHIFenceDX12()
    {
        if (fence_event)
        {
            CloseHandle(fence_event);
            fence_event = nullptr;
        }
        fence.Reset();
    }

    uint64 RHIFenceDX12::GetCompletedValue() const
    {
        if (!fence)
        {
            return 0;
        }
        return fence->GetCompletedValue();
    }

    void RHIFenceDX12::Wait(uint64 value)
    {
        if (!fence || !fence_event)
        {
            return;
        }

        if (fence->GetCompletedValue() >= value)
        {
            return;
        }

        if (FAILED(fence->SetEventOnCompletion(value, fence_event)))
        {
            backlog::Post("Failed to set fence completion event", backlog::LogLevel::Error);
            return;
        }

        WaitForSingleObject(fence_event, INFINITE);
    }

    uint64 RHIFenceDX12::Signal(ID3D12CommandQueue& queue)
    {
        if (!fence)
        {
            return 0;
        }

        ++next_value;
        if (FAILED(queue.Signal(fence.Get(), next_value)))
        {
            backlog::Post("Failed to signal fence from command queue", backlog::LogLevel::Error);
            return 0;
        }

        return next_value;
    }

    ID3D12Fence* RHIFenceDX12::GetFence() const
    {
        return fence.Get();
    }
}
