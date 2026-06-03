#pragma once
#include "ShaderCompiler.h"

namespace won::resource
{
    inline constexpr uint32 shader_cache_version = 1;

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
        CSTextureBC1Compress,
        CSTextureBC3Compress,
        CSTextureBC4Compress,
        CSTextureBC5Compress,

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
