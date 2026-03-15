#pragma once
#include "ShaderCompiler.h"

namespace won::resource
{
    enum class ShaderId : uint16
    {
        VSObjectCommon,
        VSObjectSimple,
        VSObjectPrepass,
        PSObjectCommon,
        PSObjectSimple,
        PSObjectPrepass,
        PSTestRed,
        Count
    };

    struct ShaderManifestEntry
    {
        ShaderId shader_id = ShaderId::Count;
        ShaderCompileDesc compile_desc = {};
    };

    using ShaderManifest = Vector<ShaderManifestEntry>;

    WONENGINE_API const ShaderManifest& GetDefaultShaderManifest();
}
