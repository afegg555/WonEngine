#pragma once
#include "RHIQueryHeap.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12QueryHeap;

namespace won::rendering
{
    class RHIQueryHeapDX12 final : public RHIQueryHeap
    {
    public:
        RHIQueryHeapDX12(const RHIQueryHeapDesc& desc_in, ComPtr<ID3D12QueryHeap> query_heap_in);

        void SetName(const String& name) override;
        const String& GetName() const override;
        const RHIQueryHeapDesc& GetDesc() const override;
        ID3D12QueryHeap* GetQueryHeap() const;

    private:
        RHIQueryHeapDesc desc = {};
        String name;
        ComPtr<ID3D12QueryHeap> query_heap;
    };
}
