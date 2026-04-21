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
            { ShaderId::PSSky, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "SkyPS.hlsl", "main" } },
            { ShaderId::PSObjectCommon, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_common.hlsl", "main" } },
            { ShaderId::PSObjectSimple, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_simple.hlsl", "main" } },
            { ShaderId::PSObjectPrepass, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "ObjectPS_prepass.hlsl", "main" } },
            { ShaderId::PSTestRed, { rendering::RHIShaderStage::Pixel, ShaderFormat::HLSL6, ShaderModel::SM_6_0, "TestRedPS.hlsl", "main" } },
        };

        return manifest;
    }
}
