#include "RHIPipelineDX12.h"

#include <algorithm>

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
        param_infos.clear();

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
            auto& param_info = param_infos.emplace_back();
            switch (param.ParameterType)
            {
            case D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE:
            {
                if (param.DescriptorTable.NumDescriptorRanges == 0)
                {
                    param_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                    break;
                }

                if (param.DescriptorTable.pDescriptorRanges[0].RegisterSpace > 0)
                {
                    param_info.is_bindless = true;
                }
                if (param.DescriptorTable.pDescriptorRanges[0].RangeType == D3D12_DESCRIPTOR_RANGE_TYPE_SAMPLER)
                {
                    param_info.is_sampler = true;
                }
                uint32 next_table_offset = 0;
                for (uint32 range_index = 0; range_index < param.DescriptorTable.NumDescriptorRanges; ++range_index)
                {
                    const auto& range = param.DescriptorTable.pDescriptorRanges[range_index];
                    const uint32 descriptor_count = range.NumDescriptors == UINT_MAX ? 0 : range.NumDescriptors;
                    const uint32 table_offset = range.OffsetInDescriptorsFromTableStart == D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND ?
                        next_table_offset :
                        range.OffsetInDescriptorsFromTableStart;
                    next_table_offset = descriptor_count > 0 ? (std::max)(next_table_offset, table_offset + descriptor_count) : next_table_offset;

                    RHIPipelineDX12::RootSignatureBindingTable::ParamInfo::DescriptorRangeInfo range_info = {};
                    range_info.range_type = range.RangeType;
                    range_info.base_register = range.BaseShaderRegister;
                    range_info.register_space = range.RegisterSpace;
                    range_info.descriptor_count = descriptor_count;
                    range_info.table_offset = table_offset;
                    param_info.descriptor_ranges.push_back(range_info);

                    if (param_info.is_bindless || descriptor_count == 0)
                    {
                        continue;
                    }

                    param_info.table_descriptor_count = (std::max)(param_info.table_descriptor_count, table_offset + descriptor_count);

                    for (uint32 descriptor_index = 0; descriptor_index < descriptor_count; ++descriptor_index)
                    {
                        const uint32 shader_register = range.BaseShaderRegister + descriptor_index;
                        switch (range.RangeType)
                        {
                        case D3D12_DESCRIPTOR_RANGE_TYPE_CBV:
                            if (shader_register < descriptor_binder_cbv_count)
                            {
                                cbv[shader_register] = static_cast<uint8>(i);
                            }
                            break;
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

                param_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS:
            {
                param_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_CBV:
            {
                param_info.shader_register = param.Descriptor.ShaderRegister;
                param_info.register_space = param.Descriptor.RegisterSpace;
                if (param.Descriptor.RegisterSpace == 0 && param.Descriptor.ShaderRegister < descriptor_binder_cbv_count)
                {
                    cbv[param.Descriptor.ShaderRegister] = static_cast<uint8>(i);
                }

                param_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_CBV;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_SRV:
            {
                param_info.shader_register = param.Descriptor.ShaderRegister;
                param_info.register_space = param.Descriptor.RegisterSpace;
                if (param.Descriptor.RegisterSpace == 0 && param.Descriptor.ShaderRegister < descriptor_binder_srv_count)
                {
                    srv[param.Descriptor.ShaderRegister] = static_cast<uint8>(i);
                }

                param_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_SRV;
                break;
            }
            case D3D12_ROOT_PARAMETER_TYPE_UAV:
            {
                param_info.shader_register = param.Descriptor.ShaderRegister;
                param_info.register_space = param.Descriptor.RegisterSpace;
                if (param.Descriptor.RegisterSpace == 0 && param.Descriptor.ShaderRegister < descriptor_binder_uav_count)
                {
                    uav[param.Descriptor.ShaderRegister] = static_cast<uint8>(i);
                }

                param_info.slot_type = D3D12_ROOT_PARAMETER_TYPE_UAV;
                break;
            }
            default:
                break;
            }
        }
    }
}
