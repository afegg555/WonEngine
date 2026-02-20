#pragma once
#include "RHIContext.h"
#include "DirectX-Headers/d3d12.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace won::rendering
{
    class RHIFenceDX12 final : public RHIFence
    {
    public:
        RHIFenceDX12(ComPtr<ID3D12Device> device_in, uint64 initial_value);
        ~RHIFenceDX12() override;

        uint64 GetCompletedValue() const override;
        void Wait(uint64 value) override;

        uint64 Signal(ID3D12CommandQueue& queue);
        ID3D12Fence* GetFence() const;

    private:
        ComPtr<ID3D12Fence> fence;
        HANDLE fence_event = nullptr;
        uint64 next_value = 0;
    };
}
