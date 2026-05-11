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
                }
                else
                {
                    SetShader(entry.shader_id, shader);
                }
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
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSFullTriangle).get();
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSSky).get();
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { rtv_format };
        GraphicsPipelineHash pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::SkyPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectPrepass).get();
        pipeline_desc.pixel_shader = nullptr;
        pipeline_desc.sample_count = 1;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = {};
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::ShadowPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectPrepass).get();
        pipeline_desc.pixel_shader = nullptr;
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = {};
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::DepthPrepass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

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
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Equal);

        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.fill_mode = RHIFillMode::Wireframe;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectSimple).get();
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectSimple).get();
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Wireframe);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSPrimitive).get();
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSPrimitive).get();
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = true;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::LineList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::LineList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.topology = RHIPrimitiveTopology::PointList;
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::PointList);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSSprite3D).get();
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectSprite).get();
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = true;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite3DPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

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

    std::shared_ptr<rendering::RHIPipeline> ShaderLibrary::GetPipeline(GraphicsPipelineHash pipeline_hash) const
    {
        if (!pipeline_hash.IsValid())
        {
            return nullptr;
        }

        auto it = graphics_pipeline_cache.find(pipeline_hash.storage.value);
        if (it == graphics_pipeline_cache.end())
        {
            assert(false && "Failed to get pipeline");
            return nullptr;
        }

        return it->second;
    }

    std::shared_ptr<rendering::RHIPipeline> ShaderLibrary::GetPipeline(ComputePipelineHash pipeline_hash) const
    {
        if (!pipeline_hash.IsValid())
        {
            return nullptr;
        }

        auto it = compute_pipeline_cache.find(pipeline_hash.storage.value);
        if (it == compute_pipeline_cache.end())
        {
            assert(false && "Failed to get pipeline");
            return nullptr;
        }

        return it->second;
    }

    void ShaderLibrary::ClearPipelines()
    {
        graphics_pipeline_cache.clear();
        compute_pipeline_cache.clear();
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

    ShaderLibrary& GetShaderLibrary()
    {
        static ShaderLibrary shader_library;
        return shader_library;
    }
}
