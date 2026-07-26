#pragma once
#include "RuntimeExport.h"
#include "ShaderManifest.h"
#include "RHIShader.h"

namespace won::resource::shaderloader
{
    WONENGINE_API bool LoadShader(
        const std::unique_ptr<ShaderCompiler>& shader_compiler,
        const ShaderManifestEntry& entry,
        std::shared_ptr<rendering::RHIShader>& out_shader);

    // Writes the shader manifest (ids, resolved binary/dependency paths, stage/model/...)
    // as a JSON array to out_file_path. bin_root is the root the binary paths resolve against.
    WONENGINE_API bool DumpShaderMetadata(const String& bin_root, const String& out_file_path);
}
