#pragma once
#include "RHIPipeline.h"
#include "DirectX-Headers/d3d12.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12PipelineState;
struct ID3D12RootSignature;

namespace won::rendering
{
    class RHIPipelineDX12 final : public RHIPipeline
    {
    public:
        RHIPipelineDX12(bool is_compute_pipeline, ComPtr<ID3D12PipelineState> pipeline_state_in,
            ComPtr<ID3D12RootSignature> root_signature_in);

        bool IsCompute() const override;
        void SetName(const String& new_name) override;
        const String& GetName() const override;

        ID3D12PipelineState* GetPipelineState() const;
        ID3D12RootSignature* GetRootSignature() const;

    private:
        bool is_compute = false;
        String name;
        ComPtr<ID3D12PipelineState> pipeline_state;
        ComPtr<ID3D12RootSignature> root_signature;
    };
}

