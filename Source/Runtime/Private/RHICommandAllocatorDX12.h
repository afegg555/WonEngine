#pragma once

#include "RHICommandAllocator.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12Device;
struct ID3D12CommandAllocator;

namespace won::rendering
{
    class RHICommandAllocatorDX12 final : public RHICommandAllocator
    {
    public:
        RHICommandAllocatorDX12(RHIQueueType type, ComPtr<ID3D12Device> device_in);

        RHIQueueType GetType() const override;
        void Reset() override;

        void SetName(const String& name) override;
        const String& GetName() const override;

        ID3D12CommandAllocator* GetAllocator() const;

    private:
        RHIQueueType queue_type = RHIQueueType::Graphics;
        String name;
        ComPtr<ID3D12Device> device;
        ComPtr<ID3D12CommandAllocator> allocator;
    };
}
