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
        VSSprite2D,
        VSSprite3D,

        PSSky,
        PSObjectCommon,
        PSObjectUnlit,
        PSObjectPrepass,
        PSSprite,
        PSText3D,

        CSDDGIProbeUpdate,
        CSTextureMipGen,
        CSGPUBVHBuildGeneratePrimitives,
        CSGPUBVHBuildSortPrimitives,
        CSGPUBVHBuildBuildNodes,
        CSGPUBVHBuildReduceBounds,

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
