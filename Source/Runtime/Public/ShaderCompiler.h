#pragma once
#include "Types.h"
#include "RuntimeExport.h"
#include "RHIShader.h" // only for using enum

namespace won::resource
{
    enum class ShaderCompilerBackend
    {
        DXC
    };

    enum class ShaderFormat : uint8_t
    {
        None, // Not used
        HLSL6, // DXIL
    };

    inline constexpr const char* ToString(ShaderFormat format)
    {
        switch (format)
        {
        case ShaderFormat::None: return "None";
        case ShaderFormat::HLSL6: return "HLSL6";
        }
        return "Unknown";
    }

    enum class ShaderModel : uint8_t
    {
        SM_6_0,
        SM_6_1,
        SM_6_2,
        SM_6_3,
        SM_6_4,
        SM_6_5,
        SM_6_6,
        SM_6_7,
    };

    inline constexpr const char* ToString(ShaderModel model)
    {
        switch (model)
        {
        case ShaderModel::SM_6_0: return "SM_6_0";
        case ShaderModel::SM_6_1: return "SM_6_1";
        case ShaderModel::SM_6_2: return "SM_6_2";
        case ShaderModel::SM_6_3: return "SM_6_3";
        case ShaderModel::SM_6_4: return "SM_6_4";
        case ShaderModel::SM_6_5: return "SM_6_5";
        case ShaderModel::SM_6_6: return "SM_6_6";
        case ShaderModel::SM_6_7: return "SM_6_7";
        }
        return "Unknown";
    }

    struct ShaderCompilerOptions
    {
        ShaderCompilerBackend backend = ShaderCompilerBackend::DXC;
        String shader_source_root_path;
        String shader_bin_root_path;
    };

    struct ShaderCompileDesc
    {
        won::rendering::RHIShaderStage stage = won::rendering::RHIShaderStage::Vertex;
        ShaderFormat format = ShaderFormat::HLSL6;
        ShaderModel model = ShaderModel::SM_6_0;
        String source_file_name;
        String entry_point = "main";
    };

    struct ShaderCompileResult
    {
        Vector<uint8> bytecode;
        Vector<String> dependencies;
    };

    class ShaderCompiler
    {
    public:
        explicit ShaderCompiler(const ShaderCompilerOptions& options = {})
            : compiler_options(options) {};
        virtual ~ShaderCompiler() = default;
        virtual ShaderCompileResult Compile(const ShaderCompileDesc& desc) const = 0;

        const ShaderCompilerOptions& GetCompileOptions() const
        {
            return compiler_options;
        }
    protected:
        ShaderCompilerOptions compiler_options = {};
    };

    WONENGINE_API std::shared_ptr<ShaderCompiler> CreateShaderCompiler(const ShaderCompilerOptions& options = {});
}
