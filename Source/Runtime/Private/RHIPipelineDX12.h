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
        struct RootSignatureBindingTable
        {
            static constexpr uint8 invalid_root_parameter = 0xFF;
            uint64 slot_usage = 0ull;

            // map slot number to parameter
            uint8 cbv[descriptor_binder_cbv_count] = {};
            uint8 srv[descriptor_binder_srv_count] = {};
            uint8 uav[descriptor_binder_uav_count] = {};
            uint8 sam[descriptor_binder_sampler_count] = {};

            struct ParamInfo
            {
                struct DescriptorRangeInfo
                {
                    D3D12_DESCRIPTOR_RANGE_TYPE range_type = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
                    uint32 base_register = 0;
                    uint32 register_space = 0;
                    uint32 descriptor_count = 0;
                    uint32 table_offset = 0;
                };

                D3D12_ROOT_PARAMETER_TYPE slot_type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                bool is_sampler = false;
                bool is_bindless = false; // if space > 0
                uint32 table_descriptor_count = 0;
                uint32 shader_register = UINT_MAX;
                uint32 register_space = 0;
                Vector<DescriptorRangeInfo> descriptor_ranges;
            };
            Vector<ParamInfo> param_infos;

            void Init();
            void Build(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc);
        } binding_table;

        RHIPipelineDX12(bool is_compute_pipeline, ComPtr<ID3D12PipelineState> pipeline_state_in,
            ComPtr<ID3D12RootSignature> root_signature_in, RootSignatureBindingTable binding_table_in);

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
