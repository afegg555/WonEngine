#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIObject.h"

namespace won::rendering
{
    enum class RHIShaderStage
    {
        Vertex,
        Pixel,
        Compute
    };

    class WONENGINE_API RHIShader final : public RHIObject
    {
    public:
        RHIShader() = default;
        RHIShader(RHIShaderStage stage, const void* bytecode_data, Size bytecode_size)
            : stage(stage)
        {
            SetBytecode(bytecode_data, bytecode_size);
        }

        ~RHIShader() override = default;

        void SetStage(RHIShaderStage new_stage)
        {
            stage = new_stage;
        }

        RHIShaderStage GetStage() const
        {
            return stage;
        }

        void SetBytecode(const void* bytecode_data, Size bytecode_size)
        {
            bytecode.clear();
            if (!bytecode_data || bytecode_size == 0)
            {
                return;
            }

            bytecode.resize(bytecode_size);
            std::memcpy(bytecode.data(), bytecode_data, bytecode_size);
        }

        const void* GetBytecode() const
        {
            return bytecode.empty() ? nullptr : bytecode.data();
        }

        Size GetBytecodeSize() const
        {
            return bytecode.size();
        }

        void SetName(const String& new_name) override
        {
            name = new_name;
        }

        const String& GetName() const override
        {
            return name;
        }

    private:
        RHIShaderStage stage = RHIShaderStage::Vertex;
        Vector<uint8> bytecode;
        String name;
    };
}
