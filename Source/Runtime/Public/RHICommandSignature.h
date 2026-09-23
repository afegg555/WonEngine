#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIObject.h"

namespace won::rendering
{
    class RHIPipeline;

    enum class RHIIndirectArgumentType : uint32
    {
        Draw,
        DrawIndexed,
        Dispatch,
        Constant,
    };

    struct RHIIndirectArgument
    {
        RHIIndirectArgumentType type = RHIIndirectArgumentType::Draw;
        uint32 root_parameter_index = 0;
        uint32 dest_offset_in_values = 0;
        uint32 value_count = 0;
    };

    struct RHICommandSignatureDesc
    {
        Vector<RHIIndirectArgument> arguments;
        uint32 byte_stride = 0;
        RHIPipeline* root_signature_source = nullptr;
    };

    class RHICommandSignature : public RHIObject
    {
    public:
        ~RHICommandSignature() override = default;

        virtual const RHICommandSignatureDesc& GetDesc() const = 0;
    };
}
