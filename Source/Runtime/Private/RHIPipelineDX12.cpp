#include "RHIPipelineDX12.h"

namespace won::rendering
{
    RHIPipelineDX12::RHIPipelineDX12(bool is_compute_pipeline,
        ComPtr<ID3D12PipelineState> pipeline_state_in, ComPtr<ID3D12RootSignature> root_signature_in, RootSignatureBindingTable binding_table_in)
        : is_compute(is_compute_pipeline)
        , pipeline_state(std::move(pipeline_state_in))
        , root_signature(std::move(root_signature_in))
        , binding_table(std::move(binding_table_in))
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
    
    void RHIPipelineDX12::RootSignatureBindingTable::Init()
    {
        slot_usage = 0ull;
        slot_infos.clear();

        for (size_t i = 0; i < descriptor_binder_cbv_count; i++)
        {
            cbv[i] = invalid_root_parameter;
        }
        for (size_t i = 0; i < descriptor_binder_srv_count; i++)
        {
            srv[i] = invalid_root_parameter;
        }
        for (size_t i = 0; i < descriptor_binder_uav_count; i++)
        {
            uav[i] = invalid_root_parameter;
        }
        for (size_t i = 0; i < descriptor_binder_sampler_count; i++)
        {
            sam[i] = invalid_root_parameter;
        }
    }
    void RHIPipelineDX12::RootSignatureBindingTable::Build(const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& desc)
    {
        Init();

        assert(desc.Version == D3D_ROOT_SIGNATURE_VERSION_1_1);

        const auto& rs = desc.Desc_1_1;
        for (uint32 i = 0; i < rs.NumParameters; ++i)
        {
            const auto& param = rs.pParameters[i];
            slot_usage |= (1ull << i);
            auto& slot_info = slot_infos.emplace_back();
            switch (param.ParameterType)
            {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            {
                if (param.DescriptorTable.NumDescriptorRanges == 0)
                {
                    slot_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    break;
                }

                if (param.DescriptorTable.pDescriptorRanges[0].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                {
                    slot_info.is_sampler = true;
                }

                if (param.DescriptorTable.pDescriptorRanges[0].RegisterSpace > 0)
                {
                    slot_info.is_bindless = true;
                }
                else
                {
                    for (uint32 range_index = 0; range_index < param.DescriptorTable.NumDescriptorRanges; ++range_index)
                    {
                        const auto& range = param.DescriptorTable.pDescriptorRanges[range_index];
                        if (range.NumDescriptors == UINT_MAX)
                        {
                            continue;
                        }

                        for (uint32 descriptor_index = 0; descriptor_index < range.NumDescriptors; ++descriptor_index)
                        {
                            const uint32 shader_register = range.BaseShaderRegister + descriptor_index;
                            switch (range.RangeType)
                            {
                            case D3D12_DESCRIPTOR_RANGE_TYPE_SRV:
                                if (shader_register < descriptor_binder_srv_count)
                                {
                                    srv[shader_register] = static_cast<uint8>(i);
                                }
                                break;
                            case D3D12_DESCRIPTOR_RANGE_TYPE_UAV:
                                if (shader_register < descriptor_binder_uav_count)
                                {
                                    uav[shader_register] = static_cast<uint8>(i);
                                }
                                break;
                            case D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER:
                                if (shader_register < descriptor_binder_sampler_count)
                                {
                                    sam[shader_register] = static_cast<uint8>(i);
                                }
                                break;
                            default:
                                break;
                            }
                        }
                    }
                }

                slot_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            {
                slot_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            {
                if (param.Descriptor.RegisterSpace == 0 && param.Descriptor.ShaderRegister < descriptor_binder_cbv_count)
                {
                    cbv[param.Descriptor.ShaderRegister] = static_cast<uint8>(i);
                }

                slot_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_CBV;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            {
                if (param.Descriptor.RegisterSpace == 0 && param.Descriptor.ShaderRegister < descriptor_binder_srv_count)
                {
                    srv[param.Descriptor.ShaderRegister] = static_cast<uint8>(i);
                }

                slot_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_SRV;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
            {
                if (param.Descriptor.RegisterSpace == 0 && param.Descriptor.ShaderRegister < descriptor_binder_uav_count)
                {
                    uav[param.Descriptor.ShaderRegister] = static_cast<uint8>(i);
                }

                slot_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_UAV;
                break;
            }
            default:
                break;
            }
        }
    }
}
