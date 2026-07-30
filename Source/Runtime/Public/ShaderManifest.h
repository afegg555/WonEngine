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
        VSDecal,
        VSDebugDraw2D,
        VSDebugDraw3D,

        PSSky,
        PSObjectForward,
        PSObjectForwardPlus,
        PSObjectUnlit,
        PSObjectPrepass,
        PSSprite,
        PSText3D,
        PSComposite,
        PSDecal,
        PSDebugDraw2D,
        PSDebugDraw3D,

        CSFXAA,
        CSTonemap,
        CSLuminanceReduce,
        CSLuminanceResolve,
        CSBRDFIntegration,
        CSSkyCapture,
        CSIrradianceConvolve,
        CSSpecularPrefilter,
        CSDDGIProbeUpdate,
        CSLightCull,
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

    inline constexpr const char* ToString(ShaderId id)
    {
        switch (id)
        {
        case ShaderId::VSFullTriangle: return "VSFullTriangle";
        case ShaderId::VSObjectCommon: return "VSObjectCommon";
        case ShaderId::VSObjectSimple: return "VSObjectSimple";
        case ShaderId::VSObjectPrepass: return "VSObjectPrepass";
        case ShaderId::VSSprite2D: return "VSSprite2D";
        case ShaderId::VSSprite3D: return "VSSprite3D";
        case ShaderId::VSDecal: return "VSDecal";
        case ShaderId::VSDebugDraw2D: return "VSDebugDraw2D";
        case ShaderId::VSDebugDraw3D: return "VSDebugDraw3D";
        case ShaderId::PSSky: return "PSSky";
        case ShaderId::PSObjectForward: return "PSObjectForward";
        case ShaderId::PSObjectForwardPlus: return "PSObjectForwardPlus";
        case ShaderId::PSObjectUnlit: return "PSObjectUnlit";
        case ShaderId::PSObjectPrepass: return "PSObjectPrepass";
        case ShaderId::PSSprite: return "PSSprite";
        case ShaderId::PSText3D: return "PSText3D";
        case ShaderId::PSComposite: return "PSComposite";
        case ShaderId::PSDecal: return "PSDecal";
        case ShaderId::PSDebugDraw2D: return "PSDebugDraw2D";
        case ShaderId::PSDebugDraw3D: return "PSDebugDraw3D";
        case ShaderId::CSFXAA: return "CSFXAA";
        case ShaderId::CSTonemap: return "CSTonemap";
        case ShaderId::CSLuminanceReduce: return "CSLuminanceReduce";
        case ShaderId::CSLuminanceResolve: return "CSLuminanceResolve";
        case ShaderId::CSBRDFIntegration: return "CSBRDFIntegration";
        case ShaderId::CSSkyCapture: return "CSSkyCapture";
        case ShaderId::CSIrradianceConvolve: return "CSIrradianceConvolve";
        case ShaderId::CSSpecularPrefilter: return "CSSpecularPrefilter";
        case ShaderId::CSDDGIProbeUpdate: return "CSDDGIProbeUpdate";
        case ShaderId::CSLightCull: return "CSLightCull";
        case ShaderId::CSTextureMipGen: return "CSTextureMipGen";
        case ShaderId::CSGPUBVHBuildGeneratePrimitives: return "CSGPUBVHBuildGeneratePrimitives";
        case ShaderId::CSGPUBVHBuildSortPrimitives: return "CSGPUBVHBuildSortPrimitives";
        case ShaderId::CSGPUBVHBuildBuildNodes: return "CSGPUBVHBuildBuildNodes";
        case ShaderId::CSGPUBVHBuildReduceBounds: return "CSGPUBVHBuildReduceBounds";
        case ShaderId::CSTextureBC1Compress: return "CSTextureBC1Compress";
        case ShaderId::CSTextureBC3Compress: return "CSTextureBC3Compress";
        case ShaderId::CSTextureBC4Compress: return "CSTextureBC4Compress";
        case ShaderId::CSTextureBC5Compress: return "CSTextureBC5Compress";
        case ShaderId::Count: return "Unknown";
        }
        return "Unknown";
    }

    struct ShaderManifestEntry
    {
        ShaderId shader_id = ShaderId::Count;
        ShaderCompileDesc compile_desc = {};
    };

    using ShaderManifest = Vector<ShaderManifestEntry>;

    WONENGINE_API const ShaderManifest& GetDefaultShaderManifest();
}
