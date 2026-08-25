#include "ShaderManifest.h"

namespace won::resource
{
    const ShaderManifest& GetDefaultShaderManifest()
    {
        static const ShaderManifest manifest = {
            { ShaderId::VSFullTriangle, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "FullTriangleVS.hlsl", "main" } },
            { ShaderId::VSObjectCommon, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectVS_common.hlsl", "main" } },
            { ShaderId::VSObjectSimple, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectVS_simple.hlsl", "main" } },
            { ShaderId::VSObjectPrepass, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectVS_prepass.hlsl", "main" } },
            { ShaderId::VSSprite2D, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "Sprite2DVS.hlsl", "main" } },
            { ShaderId::VSSprite3D, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "Sprite3DVS.hlsl", "main" } },
            { ShaderId::VSDecal, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "DecalVS.hlsl", "main" } },
#ifndef WON_SHIPPING
            { ShaderId::VSDebugDraw2D, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "DebugDraw2DVS.hlsl", "main" } },
            { ShaderId::VSDebugDraw3D, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "DebugDraw3DVS.hlsl", "main" } },
#endif
            { ShaderId::VSGrid, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "GridVS.hlsl", "main" } },
            { ShaderId::VSWater, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterVS.hlsl", "main" } },
            { ShaderId::VSWaterInfo, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterInfoVS.hlsl", "main" } },
            { ShaderId::VSWaterRippleSplat, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterRippleSplatVS.hlsl", "main" } },
            { ShaderId::VSOcclusionBox, { rendering::RHIShaderStage::Vertex, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "OcclusionBoxVS.hlsl", "main" } },
            { ShaderId::PSSky, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "SkyPS.hlsl", "main" } },
            { ShaderId::PSObjectForward, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_forward.hlsl", "main" } },
            { ShaderId::PSObjectForwardPlus, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_forwardplus.hlsl", "main" } },
            { ShaderId::PSObjectUnlit, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_unlit.hlsl", "main" } },
            { ShaderId::PSObjectPrepass, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_prepass.hlsl", "main" } },
            { ShaderId::PSObjectForwardMasked, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_forward_masked.hlsl", "main" } },
            { ShaderId::PSObjectForwardPlusMasked, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_forwardplus_masked.hlsl", "main" } },
            { ShaderId::PSObjectUnlitMasked, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_unlit_masked.hlsl", "main" } },
            { ShaderId::PSSprite, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "SpritePS.hlsl", "main" } },
            { ShaderId::PSText3D, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TextPS.hlsl", "main" } },
            { ShaderId::PSComposite, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "CompositePS.hlsl", "main" } },
            { ShaderId::PSDecal, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "DecalPS.hlsl", "main" } },
#ifndef WON_SHIPPING
            { ShaderId::PSDebugDraw2D, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "DebugDraw2DPS.hlsl", "main" } },
            { ShaderId::PSDebugDraw3D, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "DebugDraw3DPS.hlsl", "main" } },
#endif
            { ShaderId::PSGrid, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "GridPS.hlsl", "main" } },
            { ShaderId::PSWaterInfo, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterInfoPS.hlsl", "main" } },
            { ShaderId::PSWaterRippleSplat, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterRippleSplatPS.hlsl", "main" } },
            { ShaderId::PSWaterForward, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterPS_forward.hlsl", "main" } },
            { ShaderId::PSWaterForwardPlus, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterPS_forwardplus.hlsl", "main" } },
            { ShaderId::CSFXAA, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "FXAACS.hlsl", "main" } },
            { ShaderId::CSTonemap, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TonemapCS.hlsl", "main" } },
            { ShaderId::CSLuminanceReduce, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "LuminanceReduceCS.hlsl", "main" } },
            { ShaderId::CSLuminanceResolve, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "LuminanceResolveCS.hlsl", "main" } },
            { ShaderId::CSBRDFIntegration, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "BRDFIntegrationCS.hlsl", "main" } },
            { ShaderId::CSSkyCapture, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "SkyCaptureCS.hlsl", "main" } },
            { ShaderId::CSIrradianceConvolve, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "IrradianceConvolveCS.hlsl", "main" } },
            { ShaderId::CSSpecularPrefilter, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "SpecularPrefilterCS.hlsl", "main" } },
            { ShaderId::CSDDGIProbeUpdate, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "DDGIProbeUpdateCS.hlsl", "main" } },
            { ShaderId::CSLightCull, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "LightCullCS.hlsl", "main" } },
            { ShaderId::CSWaterRippleStep, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "WaterRippleStepCS.hlsl", "main" } },
            { ShaderId::CSTextureMipGen, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TextureMipGenCS.hlsl", "main" } },
            { ShaderId::CSGPUBVHBuildGeneratePrimitives, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "GPUBVHGeneratePrimitivesCS.hlsl", "main" } },
            { ShaderId::CSGPUBVHBuildSortPrimitives, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "GPUBVHSortPrimitivesCS.hlsl", "main" } },
            { ShaderId::CSGPUBVHBuildBuildNodes, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "GPUBVHBuildNodesCS.hlsl", "main" } },
            { ShaderId::CSGPUBVHBuildReduceBounds, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "GPUBVHReduceBoundsCS.hlsl", "main" } },
            { ShaderId::CSTextureBC1Compress, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TextureBC1CompressCS.hlsl", "main" } },
            { ShaderId::CSTextureBC3Compress, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TextureBC3CompressCS.hlsl", "main" } },
            { ShaderId::CSTextureBC4Compress, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TextureBC4CompressCS.hlsl", "main" } },
            { ShaderId::CSTextureBC5Compress, { rendering::RHIShaderStage::Compute, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TextureBC5CompressCS.hlsl", "main" } },
        };

        return manifest;
    }
}
