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
        VSPrimitive,
        PSSky,
        PSObjectCommon,
        PSObjectSimple,
        PSObjectPrepass,
        PSPrimitive,
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
