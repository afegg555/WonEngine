#pragma once
#include "ShaderCompiler.h"

namespace won::resource
{
    enum class ShaderId : uint16
    {
        VSFullTriangle,
        VSObjectCommon,
        VSObjectSimple,
        VSObjectPrepass,
        PSSky,
        PSObjectCommon,
        PSObjectSimple,
        PSObjectPrepass,
        CSDDGIProbeUpdate,
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
