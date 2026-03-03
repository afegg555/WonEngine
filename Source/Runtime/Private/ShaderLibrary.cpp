#include "ShaderLibrary.h"
#include "Backlog.h"
#include "JobSystem.h"

using namespace won::rendering;

namespace won::resource
{
    namespace
    {
        inline constexpr Size ToIndex(ShaderId shader_id)
        {
            return static_cast<Size>(shader_id);
        }
        inline constexpr Size ToIndex(RenderPassType pass_type)
        {
            return static_cast<Size>(pass_type);
        }
    }

    ShaderLibrary::ShaderLibrary(const ShaderCompilerOptions& options)
        : compiler_options(options), shader_compiler(CreateShaderCompiler(options)), shaders(static_cast<Size>(ShaderId::Count))
    {
    }

    bool ShaderLibrary::LoadAllShaders()
    {
        jobsystem::Context ctx;

        jobsystem::Execute(ctx, [this](jobsystem::JobArgs args) { LoadShader(ShaderId::VSObjectCommon, RHIShaderStage::Vertex, "ObjectVS_common.hlsl"); });
        jobsystem::Execute(ctx, [this](jobsystem::JobArgs args) { LoadShader(ShaderId::VSObjectSimple, RHIShaderStage::Vertex, "ObjectVS_simple.hlsl"); });
        jobsystem::Execute(ctx, [this](jobsystem::JobArgs args) { LoadShader(ShaderId::VSObjectPrepass, RHIShaderStage::Vertex, "ObjectVS_prepass.hlsl"); });
        jobsystem::Execute(ctx, [this](jobsystem::JobArgs args) { LoadShader(ShaderId::PSObjectCommon, RHIShaderStage::Pixel, "ObjectPS_common.hlsl"); });
        jobsystem::Execute(ctx, [this](jobsystem::JobArgs args) { LoadShader(ShaderId::PSObjectSimple, RHIShaderStage::Pixel, "ObjectPS_simple.hlsl"); });
        jobsystem::Execute(ctx, [this](jobsystem::JobArgs args) { LoadShader(ShaderId::PSObjectPrepass, RHIShaderStage::Pixel, "ObjectPS_prepass.hlsl"); });
        jobsystem::Execute(ctx, [this](jobsystem::JobArgs args) { LoadShader(ShaderId::PSTestRed, RHIShaderStage::Pixel, "TestRedPS.hlsl"); });

        jobsystem::Wait(ctx);
        return true;
    }

    bool ShaderLibrary::BuildAllGraphicsPipelines(std::shared_ptr<rendering::RHIDevice>& device, RHIFormat rtv_format, RHIFormat dsv_format, uint32 sample_count)
    {
        if (!device)
        {
            backlog::Post("BuildAllGraphicsPipelines failed: device is null", backlog::LogLevel::Error);
            return false;
        }

        ClearPipelines();

        RHIGraphicsPipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectPrepass).get();
        pipeline_desc.pixel_shader = nullptr;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = {};
        graphics_pipelines[ToIndex(RenderPassType::DepthPrepass)] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectCommon).get();
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectCommon).get();
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::Equal;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = { rtv_format };
        graphics_pipelines[ToIndex(RenderPassType::MainPass)] = device->CreateGraphicsPipeline(pipeline_desc);

        return true;
    }

    bool ShaderLibrary::LoadShader(ShaderId shader_id, RHIShaderStage stage, const String& source_path, const String& entry_point, ShaderModel model, ShaderFormat format)
    {
        ShaderCompileDesc desc = {};
        desc.stage = stage;
        desc.source_path = source_path;
        desc.entry_point = entry_point;
        desc.model = model;
        desc.format = format;
        return LoadShader(shader_id, desc);
    }

    bool ShaderLibrary::LoadShader(ShaderId shader_id, const ShaderCompileDesc& desc)
    {
        if (!shader_compiler)
        {
            backlog::Post("Shader compiler is not initialized", backlog::LogLevel::Error);
            return false;
        }

        if (shader_id == ShaderId::Count)
        {
            backlog::Post("Invalid shader id", backlog::LogLevel::Error);
            return false;
        }

        ShaderBytecode shader_bytecode = shader_compiler->Compile(desc);
        if (shader_bytecode.bytecode.empty())
        {
            String log = "Shader compilation failed : ";
            if (!desc.source_path.empty())
            {
                log += desc.source_path;
            }
            backlog::Post(log, backlog::LogLevel::Error);
            return false;
        }
        String log = "Shader compiled : ";
        log += desc.source_path;
        backlog::Post(log);

        auto shader = std::make_shared<RHIShader>(desc.stage, shader_bytecode.bytecode.data(), shader_bytecode.bytecode.size());
        shaders[ToIndex(shader_id)] = shader;
        return true;
    }

    std::shared_ptr<rendering::RHIShader> ShaderLibrary::GetShader(ShaderId shader_id) const
    {
        if (shader_id == ShaderId::Count)
        {
            return nullptr;
        }
        return shaders[ToIndex(shader_id)];
    }

    std::shared_ptr<rendering::RHIPipeline> ShaderLibrary::GetPipeline(RenderPassType pass_type) const
    {
        if (pass_type == RenderPassType::Count)
        {
            return nullptr;
        }

        return graphics_pipelines[ToIndex(pass_type)];
    }

    void ShaderLibrary::ClearPipelines()
    {
        for (auto& pipeline : graphics_pipelines)
        {
            pipeline = nullptr;
        }
    }

    void ShaderLibrary::ClearShaders()
    {
        for (auto& shader : shaders)
        {
            shader = nullptr;
        }
    }

    void ShaderLibrary::ClearAll()
    {
        ClearPipelines();
        ClearShaders();
    }

    Size ShaderLibrary::GetShaderCount() const
    {
        Size count = 0;
        for (const auto& shader : shaders)
        {
            if (shader)
            {
                ++count;
            }
        }
        return count;
    }
}
