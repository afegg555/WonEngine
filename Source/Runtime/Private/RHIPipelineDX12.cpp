#include "RHIPipelineDX12.h"

namespace won::rendering
{
    RHIPipelineDX12::RHIPipelineDX12(bool is_compute_pipeline,
        ComPtr<ID3D12PipelineState> pipeline_state_in, ComPtr<ID3D12RootSignature> root_signature_in)
        : is_compute(is_compute_pipeline)
        , pipeline_state(std::move(pipeline_state_in))
        , root_signature(std::move(root_signature_in))
    {
    }

    bool RHIPipelineDX12::IsCompute() const
    {
        return is_compute;
    }

    void RHIPipelineDX12::SetName(const String& new_name)
    {
        name = new_name;
    }

    const String& RHIPipelineDX12::GetName() const
    {
        return name;
    }

    ID3D12PipelineState* RHIPipelineDX12::GetPipelineState() const
    {
        return pipeline_state.Get();
    }

    ID3D12RootSignature* RHIPipelineDX12::GetRootSignature() const
    {
        return root_signature.Get();
    }
}

