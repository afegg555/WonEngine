#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "ShaderCompiler.h"
#include "RHIShader.h"
#include "RHIDevice.h"

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

    enum class RenderPassType : uint8
    {
        DepthPrepass,
        MainPass,
        Count
    };

    class WONENGINE_API ShaderLibrary
    {
    public:
        explicit ShaderLibrary(const ShaderCompilerOptions& options = {});
        bool LoadAllShaders();
        bool BuildAllGraphicsPipelines(const std::shared_ptr<rendering::RHIDevice>& device, rendering::RHIFormat rtv_format, rendering::RHIFormat dsv_format, uint32 sample_count);
        bool LoadShader(ShaderId shader_id, rendering::RHIShaderStage stage, const String& source_path, const String& entry_point = "main", ShaderModel model = ShaderModel::SM_6_0, ShaderFormat format = ShaderFormat::HLSL6);
        bool LoadShader(ShaderId shader_id, const ShaderCompileDesc& desc);
        bool IsShaderOutdated(String cso_name) const;
        bool SaveShaderCompileResult(const ShaderCompileResult& result, const String& cso_name) const;
        
        std::shared_ptr<rendering::RHIShader> GetShader(ShaderId shader_id) const;
        std::shared_ptr<rendering::RHIPipeline> GetPipeline(RenderPassType pass_type) const;
        void ClearPipelines();
        void ClearShaders();
        void ClearAll();
        Size GetShaderCount() const;

    private:
        std::shared_ptr<rendering::RHIDevice> device;
        ShaderCompilerOptions compiler_options = {};
        std::shared_ptr<ShaderCompiler> shader_compiler = {};
        Vector<std::shared_ptr<rendering::RHIShader>> shaders;
        std::array<std::shared_ptr<rendering::RHIPipeline>, static_cast<Size>(RenderPassType::Count)> graphics_pipelines = {};
    };
}
