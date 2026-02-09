#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "ShaderCompiler.h"
#include "RHIShader.h"

namespace won::resource
{
    enum class ShaderId : uint16
    {
        TestTriangleVS,
        TestRedPS,
        Count
    };

    class WONENGINE_API ShaderLibrary
    {
    public:
        explicit ShaderLibrary(const ShaderCompilerOptions& options = {});
        bool LoadAllShaders();
        bool LoadShader(ShaderId shader_id, rendering::RHIShaderStage stage, const String& source_path, const String& entry_point = "main", ShaderModel model = ShaderModel::SM_6_0, ShaderFormat format = ShaderFormat::HLSL6);
        bool LoadShader(ShaderId shader_id, const ShaderCompileDesc& desc);
        std::shared_ptr<rendering::RHIShader> GetShader(ShaderId shader_id) const;
        void Clear();
        Size GetShaderCount() const;

    private:
        static Size ToIndex(ShaderId shader_id);
        static const char* ToName(ShaderId shader_id);
        ShaderCompilerOptions compiler_options = {};
        std::shared_ptr<ShaderCompiler> shader_compiler = {};
        Vector<std::shared_ptr<rendering::RHIShader>> shaders;
    };
}
