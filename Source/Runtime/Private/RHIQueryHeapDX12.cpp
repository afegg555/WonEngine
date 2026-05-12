#include "RHIQueryHeapDX12.h"

#include "StringUtils.h"

#include "DirectX-Headers/d3d12.h"

namespace won::rendering
{
    RHIQueryHeapDX12::RHIQueryHeapDX12(const RHIQueryHeapDesc& desc_in, ComPtr<ID3D12QueryHeap> query_heap_in)
        : desc(desc_in)
        , query_heap(std::move(query_heap_in))
    {
    }

    void RHIQueryHeapDX12::SetName(const String& new_name)
    {
        name = new_name;
        if (!query_heap)
        {
            return;
        }

        query_heap->SetName(won::utils::DecodeUtf8(name).c_str());
    }

    const String& RHIQueryHeapDX12::GetName() const
    {
        return name;
    }

    const RHIQueryHeapDesc& RHIQueryHeapDX12::GetDesc() const
    {
        return desc;
    }

    ID3D12QueryHeap* RHIQueryHeapDX12::GetQueryHeap() const
    {
        return query_heap.Get();
    }
}
