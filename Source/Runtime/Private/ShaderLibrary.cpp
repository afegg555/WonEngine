#include "ShaderLibrary.h"
#include "ShaderLoader.h"
#include "Backlog.h"
#include "JobSystem.h"
#include <atomic>

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
        : compiler_options(options), shader_compiler(CreateShaderCompiler(options))
    {
    }

    bool ShaderLibrary::LoadManifest(const ShaderManifest& manifest)
    {
        jobsystem::Context ctx;
        std::atomic<bool> load_succeeded = true;

        for (const auto& entry : manifest)
        {
            jobsystem::Execute(ctx, [this, &load_succeeded, entry](jobsystem::JobArgs args) {
                std::shared_ptr<RHIShader> shader;
                if (!shaderloader::LoadShader(shader_compiler, entry, shader))
                {
                    load_succeeded.store(false);
                    return;
                }

                SetShader(entry.shader_id, shader);
            });
        }

        jobsystem::Wait(ctx);
        return load_succeeded.load();
    }

    bool ShaderLibrary::BuildAllGraphicsPipelines(const std::shared_ptr<rendering::RHIDevice>& device, RHIFormat rtv_format, RHIFormat dsv_format, uint32 sample_count)
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

    void ShaderLibrary::SetShader(ShaderId shader_id, const std::shared_ptr<rendering::RHIShader>& shader)
    {
        if (shader_id == ShaderId::Count)
        {
            return;
        }

        shaders[ToIndex(shader_id)] = shader;
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
