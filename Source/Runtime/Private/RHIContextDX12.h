#pragma once
#include "RHIContext.h"

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace won::rendering
{
    class RHIContextDX12 final : public RHIContext
    {
    public:
        RHIContextDX12(RHIQueueType type, ComPtr<ID3D12Device> device_in);
        bool IsValid() const;

        RHIQueueType GetType() const override;
        uint64 Submit(RHICommandList& command_list, RHIFence* fence = nullptr) override;
        uint64 Submit(const Vector<RHICommandList*>& command_lists, RHIFence* fence = nullptr) override;
        void Wait(RHIFence& fence, uint64 value) override;
        void WaitIdle() override;
        uint64 GetTimestampFrequency() const override;
        ID3D12CommandQueue* GetQueue() const;

    private:
        RHIQueueType queue_type = RHIQueueType::Graphics;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandQueue> queue;
    };
}
