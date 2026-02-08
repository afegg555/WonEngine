#include "RHICommandAllocatorDX12.h"

#include "Backlog.h"

#include "DirectX-Headers/d3d12.h"

namespace won::rendering
{
    RHICommandAllocatorDX12::RHICommandAllocatorDX12(RHIQueueType type,
        ComPtr<ID3D12Device> device_in)
        : queue_type(type)
        , device(std::move(device_in))
    {
        if (!device)
        {
            backlog::Post("DX12 device is not initialized", backlog::LogLevel::Error);
            return;
        }

        D3D12_COMMAND_LIST_TYPE list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        switch (queue_type)
        {
        case RHIQueueType::Graphics:
            list_type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            break;
        case RHIQueueType::Compute:
            list_type = D3D12_COMMAND_LIST_TYPE_COMPUTE;
            break;
        case RHIQueueType::Copy:
            list_type = D3D12_COMMAND_LIST_TYPE_COPY;
            break;
        }

        if (FAILED(device->CreateCommandAllocator(list_type, IID_PPV_ARGS(&allocator))))
        {
            backlog::Post("Failed to create command allocator", backlog::LogLevel::Error);
            allocator.Reset();
        }
    }

    RHIQueueType RHICommandAllocatorDX12::GetType() const
    {
        return queue_type;
    }

    void RHICommandAllocatorDX12::Reset()
    {
        if (allocator)
        {
            allocator->Reset();
        }
    }

    void RHICommandAllocatorDX12::SetName(const String& name_in)
    {
        name = name_in;
    }

    const String& RHICommandAllocatorDX12::GetName() const
    {
        return name;
    }

    ID3D12CommandAllocator* RHICommandAllocatorDX12::GetAllocator() const
    {
        return allocator.Get();
    }
}
