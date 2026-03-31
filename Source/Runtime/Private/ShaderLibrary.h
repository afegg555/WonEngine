#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "ShaderCompiler.h"
#include "ShaderManifest.h"
#include "RHIShader.h"
#include "RHIDevice.h"

namespace won::resource
{
    enum class RenderPassType : uint8
    {
        ShadowPass,
        DepthPrepass,
        MainPass,
        Count
    };
    // TODO: determine the responsibility for pipeline management;
    // graphics / compute shader pipeline management.
    class ShaderLibrary
    {
    public:
        explicit ShaderLibrary(const ShaderCompilerOptions& options = {});
        bool LoadManifest(const ShaderManifest& manifest);
        bool BuildAllGraphicsPipelines(const std::shared_ptr<rendering::RHIDevice>& device, rendering::RHIFormat rtv_format, rendering::RHIFormat dsv_format, uint32 sample_count);
        void SetShader(ShaderId shader_id, const std::shared_ptr<rendering::RHIShader>& shader);
        
        std::shared_ptr<rendering::RHIShader> GetShader(ShaderId shader_id) const;
        std::shared_ptr<rendering::RHIPipeline> GetPipeline(RenderPassType pass_type) const;
        void ClearPipelines();
        void ClearShaders();
        void ClearAll();
        Size GetShaderCount() const;

    private:
        ShaderCompilerOptions compiler_options = {};
        std::shared_ptr<ShaderCompiler> shader_compiler = {};
        std::array<std::shared_ptr<rendering::RHIShader>, static_cast<Size>(ShaderId::Count)> shaders;
        std::array<std::shared_ptr<rendering::RHIPipeline>, static_cast<Size>(RenderPassType::Count)> graphics_pipelines = {};
    };
}
