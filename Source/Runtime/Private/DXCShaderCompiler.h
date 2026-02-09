#pragma once
#include "ShaderCompiler.h"

#if defined(_WIN32)
#include <wrl/client.h>
using Microsoft::WRL::ComPtr;
#include <dxcapi.h>
#endif

namespace won::resource
{
    class DXCShaderCompiler final : public ShaderCompiler
    {
    public:
        explicit DXCShaderCompiler(const ShaderCompilerOptions& options);
        ShaderBytecode Compile(const ShaderCompileDesc& desc) const override;

    private:
        ShaderCompilerOptions compiler_options = {};
#if defined(_WIN32)
        ComPtr<IDxcUtils> dxc_utils;
        ComPtr<IDxcCompiler3> dxc_compiler;
#endif
    };
}
