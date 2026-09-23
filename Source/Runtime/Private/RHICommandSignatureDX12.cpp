#include "RHICommandSignatureDX12.h"

#include "StringUtils.h"

#include "DirectX-Headers/d3d12.h"

namespace won::rendering
{
    RHICommandSignatureDX12::RHICommandSignatureDX12(const RHICommandSignatureDesc& desc_in, ComPtr<ID3D12CommandSignature> command_signature_in)
        : desc(desc_in)
        , command_signature(std::move(command_signature_in))
    {
    }

    void RHICommandSignatureDX12::SetName(const String& new_name)
    {
        name = new_name;
        if (!command_signature)
        {
            return;
        }

        command_signature->SetName(won::utils::DecodeUtf8(name).c_str());
    }

    const String& RHICommandSignatureDX12::GetName() const
    {
        return name;
    }

    const RHICommandSignatureDesc& RHICommandSignatureDX12::GetDesc() const
    {
        return desc;
    }

    ID3D12CommandSignature* RHICommandSignatureDX12::GetCommandSignature() const
    {
        return command_signature.Get();
    }
}
