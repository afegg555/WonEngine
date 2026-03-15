#pragma once
#include "RuntimeExport.h"
#include "ShaderManifest.h"
#include "RHIShader.h"

namespace won::resource::shaderloader
{
    WONENGINE_API bool LoadShader(
        const std::shared_ptr<ShaderCompiler>& shader_compiler,
        const ShaderManifestEntry& entry,
        std::shared_ptr<rendering::RHIShader>& out_shader);
}
