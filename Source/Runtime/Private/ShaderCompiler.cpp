#include "ShaderCompiler.h"
#include "Backlog.h"
#include "DXCShaderCompiler.h"
#include "FileSystem.h"

namespace won::resource
{
    std::unique_ptr<ShaderCompiler> CreateShaderCompiler(const ShaderCompilerOptions& options)
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

        if (resolved_options.shader_bin_root_path.empty())
        {
            const String executable_shader_bin_root_path = io::CombinePath(io::GetExecutableDirectory(), "CompiledShaders");
            if (io::IsDirectory(executable_shader_bin_root_path))
            {
                resolved_options.shader_bin_root_path = executable_shader_bin_root_path;
            }
            else
            {
#if defined(WONENGINE_SHADER_BIN_DIR)
                resolved_options.shader_bin_root_path = WONENGINE_SHADER_BIN_DIR;
#else
                resolved_options.shader_bin_root_path = "";
#endif
            }
        }

        switch (resolved_options.backend)
        {
        case ShaderCompilerBackend::DXC:
            return std::make_unique<DXCShaderCompiler>(resolved_options);
        default:
            backlog::Post("Unsupported shader compiler backend", backlog::LogLevel::Error);
            return nullptr;
        }
    }
}
