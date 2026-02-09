#include "ShaderCompiler.h"
#include "Backlog.h"
#include "DXCShaderCompiler.h"

namespace won::resource
{
    std::shared_ptr<ShaderCompiler> CreateShaderCompiler(const ShaderCompilerOptions& options)
    {
        ShaderCompilerOptions resolved_options = options;
        if (resolved_options.shader_source_root_path.empty())
        {
#if defined(WONENGINE_SHADER_SOURCE_DIR)
            resolved_options.shader_source_root_path = WONENGINE_SHADER_SOURCE_DIR;
#else
            resolved_options.shader_source_root_path = "Source/Shaders";
#endif
        }

        switch (resolved_options.backend)
        {
        case ShaderCompilerBackend::DXC:
            return std::make_shared<DXCShaderCompiler>(resolved_options);
        default:
            backlog::Post("Unsupported shader compiler backend", backlog::LogLevel::Error);
            return nullptr;
        }
    }
}
