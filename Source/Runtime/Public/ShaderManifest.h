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
        VSObjectMotion,
        VSObjectMotionMasked,
        VSSprite2D,
        VSSprite3D,
        VSDecal,
        VSDebugDraw2D,
        VSDebugDraw3D,
        VSGrid,
        VSWater,
        VSWaterInfo,
        VSWaterRippleSplat,
        VSOcclusionBox,

        PSSky,
        PSObjectForward,
        PSObjectForwardPlus,
        PSObjectUnlit,
        PSObjectPrepass,
        PSObjectMotion,
        PSObjectMotionMasked,
        PSObjectForwardMasked,
        PSObjectForwardPlusMasked,
        PSObjectUnlitMasked,
        PSSprite,
        PSSpriteMasked,
        PSText3D,
        PSComposite,
        PSDecal,
        PSDebugDraw2D,
        PSDebugDraw3D,
        PSGrid,
        PSWaterInfo,
        PSWaterRippleSplat,
        PSWaterForward,
        PSWaterForwardPlus,

        CSFXAA,
        CSTAA,
        CSTonemap,
        CSLuminanceReduce,
        CSLuminanceResolve,
        CSBRDFIntegration,
        CSMeshNormal,
        CSSkyCapture,
        CSIrradianceConvolve,
        CSSpecularPrefilter,
        CSDDGIProbeUpdate,
        CSLightCull,
        CSWaterRippleStep,
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
        case ShaderId::VSObjectMotion: return "VSObjectMotion";
        case ShaderId::VSObjectMotionMasked: return "VSObjectMotionMasked";
        case ShaderId::VSSprite2D: return "VSSprite2D";
        case ShaderId::VSSprite3D: return "VSSprite3D";
        case ShaderId::VSDecal: return "VSDecal";
        case ShaderId::VSDebugDraw2D: return "VSDebugDraw2D";
        case ShaderId::VSDebugDraw3D: return "VSDebugDraw3D";
        case ShaderId::VSGrid: return "VSGrid";
        case ShaderId::VSWater: return "VSWater";
        case ShaderId::VSWaterInfo: return "VSWaterInfo";
        case ShaderId::VSWaterRippleSplat: return "VSWaterRippleSplat";
        case ShaderId::VSOcclusionBox: return "VSOcclusionBox";
        case ShaderId::PSSky: return "PSSky";
        case ShaderId::PSObjectForward: return "PSObjectForward";
        case ShaderId::PSObjectForwardPlus: return "PSObjectForwardPlus";
        case ShaderId::PSObjectUnlit: return "PSObjectUnlit";
        case ShaderId::PSObjectPrepass: return "PSObjectPrepass";
        case ShaderId::PSObjectMotion: return "PSObjectMotion";
        case ShaderId::PSObjectMotionMasked: return "PSObjectMotionMasked";
        case ShaderId::PSObjectForwardMasked: return "PSObjectForwardMasked";
        case ShaderId::PSObjectForwardPlusMasked: return "PSObjectForwardPlusMasked";
        case ShaderId::PSObjectUnlitMasked: return "PSObjectUnlitMasked";
        case ShaderId::PSSprite: return "PSSprite";
        case ShaderId::PSSpriteMasked: return "PSSpriteMasked";
        case ShaderId::PSText3D: return "PSText3D";
        case ShaderId::PSComposite: return "PSComposite";
        case ShaderId::PSDecal: return "PSDecal";
        case ShaderId::PSDebugDraw2D: return "PSDebugDraw2D";
        case ShaderId::PSDebugDraw3D: return "PSDebugDraw3D";
        case ShaderId::PSGrid: return "PSGrid";
        case ShaderId::PSWaterInfo: return "PSWaterInfo";
        case ShaderId::PSWaterRippleSplat: return "PSWaterRippleSplat";
        case ShaderId::PSWaterForward: return "PSWaterForward";
        case ShaderId::PSWaterForwardPlus: return "PSWaterForwardPlus";
        case ShaderId::CSFXAA: return "CSFXAA";
        case ShaderId::CSTAA: return "CSTAA";
        case ShaderId::CSTonemap: return "CSTonemap";
        case ShaderId::CSLuminanceReduce: return "CSLuminanceReduce";
        case ShaderId::CSLuminanceResolve: return "CSLuminanceResolve";
        case ShaderId::CSBRDFIntegration: return "CSBRDFIntegration";
        case ShaderId::CSMeshNormal: return "CSMeshNormal";
        case ShaderId::CSSkyCapture: return "CSSkyCapture";
        case ShaderId::CSIrradianceConvolve: return "CSIrradianceConvolve";
        case ShaderId::CSSpecularPrefilter: return "CSSpecularPrefilter";
        case ShaderId::CSDDGIProbeUpdate: return "CSDDGIProbeUpdate";
        case ShaderId::CSLightCull: return "CSLightCull";
        case ShaderId::CSWaterRippleStep: return "CSWaterRippleStep";
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
