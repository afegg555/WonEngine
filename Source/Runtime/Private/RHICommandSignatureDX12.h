#pragma once
#include "RHICommandSignature.h"

#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

struct ID3D12CommandSignature;

namespace won::rendering
{
    class RHICommandSignatureDX12 final : public RHICommandSignature
    {
    public:
        RHICommandSignatureDX12(const RHICommandSignatureDesc& desc_in, ComPtr<ID3D12CommandSignature> command_signature_in);

        void SetName(const String& name) override;
        const String& GetName() const override;
        const RHICommandSignatureDesc& GetDesc() const override;
        ID3D12CommandSignature* GetCommandSignature() const;

    private:
        RHICommandSignatureDesc desc = {};
        String name;
        ComPtr<ID3D12CommandSignature> command_signature;
    };
}
