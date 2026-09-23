#include "ShaderLibrary.h"
#include "ShaderLoader.h"
#include "Backlog.h"
#include "Material.h"
#include "JobSystem.h"
#include "ShaderInterop_Renderer.h"
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

    ShaderLibrary::ShaderLibrary(rendering::RHIDevice* device, const ShaderCompilerOptions& options)
        : device(device), compiler_options(options), shader_compiler(CreateShaderCompiler(options))
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

    bool ShaderLibrary::BuildAllPipelines(RHIFormat hdr_rtv_format, RHIFormat ldr_rtv_format, RHIFormat dsv_format, uint32 sample_count)
    {
        if (!device)
        {
            backlog::Post("BuildAllPipelines failed: device is null", backlog::LogLevel::Error);
            return false;
        }

        ClearPipelines();

        RHIGraphicsPipelineDesc pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSFullTriangle);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSSky);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        GraphicsPipelineHash pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::SkyPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectPrepass);
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

        pipeline_desc.vertex_shader = GetShader(ShaderId::VSTerrainSimple);
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(ShaderId::VSTerrainSimple);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectPrepass);
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
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Prepass);
        pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(PrepassMode::DepthOnly);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.vertex_shader = GetShader(ShaderId::VSTerrainSimple);
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(ShaderId::VSTerrainSimple);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectNormal);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectNormal);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = { RHIFormat::R16G16B16A16Float };
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Prepass);
        pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(PrepassMode::Normal);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectMotion);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectMotion);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = { RHIFormat::R16G16B16A16Float };
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Prepass);
        pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(PrepassMode::Motion);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectMotionMasked);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectMotionMasked);
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Masked);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectMotionNormal);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectMotionNormal);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = { RHIFormat::R16G16B16A16Float, RHIFormat::R16G16B16A16Float };
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Prepass);
        pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(PrepassMode::MotionNormal);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectMotionNormalMasked);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectMotionNormalMasked);
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Masked);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSOcclusionBox);
        pipeline_desc.pixel_shader = nullptr;
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = {};
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::OcclusionQueryPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectCommon);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectForward);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::Equal;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Equal);
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_PBR;

        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectForwardPlus);
        pipeline_hash.storage.bits.clustered = 1;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_hash.storage.bits.clustered = 0;

        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectSimple);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlit);
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_UNLIT;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        // Masked materials are skipped by the depth prepass, so they own their depth here.
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectCommon);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectForwardMasked);
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_PBR;
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Masked);
        for (uint32 clustered = 0; clustered < 2; ++clustered)
        {
            pipeline_desc.pixel_shader = GetShader(clustered ? ShaderId::PSObjectForwardPlusMasked : ShaderId::PSObjectForwardMasked);
            pipeline_hash.storage.bits.clustered = clustered;
            pipeline_desc.raster.cull_mode = RHICullMode::Back;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            pipeline_desc.raster.cull_mode = RHICullMode::None;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        }
        pipeline_hash.storage.bits.clustered = 0;

        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectSimple);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlitMasked);
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_UNLIT;
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_hash.storage.bits.blend_mode = 0;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::Equal;
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Equal);

        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectSimple);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlit);
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_UNLIT;
        pipeline_desc.raster.fill_mode = RHIFillMode::Wireframe;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectSimple);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlit);
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Wireframe);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_UNLIT;
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlitMasked);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Masked);
        pipeline_desc.raster.cull_mode = RHICullMode::Back;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        const struct
        {
            MaterialBlendMode material_mode;
            RHIBlendMode rhi_mode;
        } blended_mesh_variants[] = {
            { MaterialBlendMode::Transparent, RHIBlendMode::Alpha },
            { MaterialBlendMode::Additive, RHIBlendMode::Additive },
            { MaterialBlendMode::Premultiplied, RHIBlendMode::Premultiplied },
        };

        for (const auto& blend_variant : blended_mesh_variants)
        {
            pipeline_desc = {};
            pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectCommon);
            pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectForward);
            pipeline_desc.sample_count = sample_count;
            pipeline_desc.depth_stencil_format = dsv_format;
            pipeline_desc.depth_stencil.depth_test = true;
            pipeline_desc.depth_stencil.depth_write = false;
            pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
            pipeline_desc.blend.enable = true;
            pipeline_desc.blend.mode = blend_variant.rhi_mode;
            pipeline_desc.render_target_formats = { hdr_rtv_format };
            pipeline_hash = {};
            pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
            pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
            pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
            pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
            pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_PBR;
            pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(blend_variant.material_mode);
            pipeline_desc.raster.cull_mode = RHICullMode::Back;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            pipeline_desc.raster.cull_mode = RHICullMode::None;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

            pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectForwardPlus);
            pipeline_hash.storage.bits.clustered = 1;
            pipeline_desc.raster.cull_mode = RHICullMode::Back;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            pipeline_desc.raster.cull_mode = RHICullMode::None;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            pipeline_hash.storage.bits.clustered = 0;

            pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectSimple);
            pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlit);
            pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_UNLIT;
            pipeline_desc.raster.cull_mode = RHICullMode::Back;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::Back);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            pipeline_desc.raster.cull_mode = RHICullMode::None;
            pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        }

        for (uint32 shader_type = 0; shader_type < SHADER_MATERIAL_TYPE_COUNT; ++shader_type)
        {
            for (uint32 blend_index = 0; blend_index < static_cast<uint32>(MaterialBlendMode::Premultiplied) + 1; ++blend_index)
            {
                const MaterialBlendMode blend_mode = static_cast<MaterialBlendMode>(blend_index);
                const bool masked = blend_mode == MaterialBlendMode::Masked;
                const bool blended = blend_mode >= MaterialBlendMode::Transparent;
                const uint32 clustered_count = shader_type == SHADER_MATERIAL_TYPE_PBR ? 2u : 1u;
                for (uint32 clustered = 0; clustered < clustered_count; ++clustered)
                {
                    ShaderId pixel_shader = ShaderId::PSTerrainUnlit;
                    if (shader_type == SHADER_MATERIAL_TYPE_PBR)
                    {
                        pixel_shader = masked
                            ? (clustered ? ShaderId::PSTerrainForwardPlusMasked : ShaderId::PSTerrainForwardMasked)
                            : (clustered ? ShaderId::PSTerrainForwardPlus : ShaderId::PSTerrainForward);
                    }
                    else if (masked)
                    {
                        pixel_shader = ShaderId::PSTerrainUnlitMasked;
                    }

                    pipeline_desc = {};
                    pipeline_desc.vertex_shader = GetShader(ShaderId::VSTerrainCommon);
                    pipeline_desc.pixel_shader = GetShader(pixel_shader);
                    pipeline_desc.sample_count = sample_count;
                    pipeline_desc.depth_stencil_format = dsv_format;
                    pipeline_desc.depth_stencil.depth_test = true;
                    pipeline_desc.depth_stencil.depth_write = masked;
                    pipeline_desc.depth_stencil.depth_compare = masked || blended ? RHICompareOp::GreaterEqual : RHICompareOp::Equal;
                    pipeline_desc.blend.enable = blended;
                    if (blend_mode == MaterialBlendMode::Transparent)
                    {
                        pipeline_desc.blend.mode = RHIBlendMode::Alpha;
                    }
                    else if (blend_mode == MaterialBlendMode::Additive)
                    {
                        pipeline_desc.blend.mode = RHIBlendMode::Additive;
                    }
                    else if (blend_mode == MaterialBlendMode::Premultiplied)
                    {
                        pipeline_desc.blend.mode = RHIBlendMode::Premultiplied;
                    }
                    pipeline_desc.render_target_formats = { hdr_rtv_format };

                    for (uint32 cull_index = 0; cull_index < 2; ++cull_index)
                    {
                        const RHICullMode cull_mode = cull_index == 0 ? RHICullMode::Back : RHICullMode::None;
                        pipeline_desc.raster.cull_mode = cull_mode;
                        pipeline_hash = {};
                        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
                        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(cull_mode);
                        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(pipeline_desc.depth_stencil.depth_compare);
                        pipeline_hash.storage.bits.shader_type = shader_type;
                        pipeline_hash.storage.bits.blend_mode = blend_index;
                        pipeline_hash.storage.bits.clustered = clustered;
                        pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(ShaderId::VSTerrainCommon);
                        pipeline_hash.storage.bits.pixel_shader = static_cast<uint64>(pixel_shader);
                        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
                    }
                }
            }
        }

        const struct
        {
            RHIFillMode fill_mode;
            RHICompareOp depth_compare;
            MaterialBlendMode blend_mode;
            bool blend_enabled;
        } view_mode_variants[] = {
            { RHIFillMode::Wireframe, RHICompareOp::GreaterEqual, MaterialBlendMode::Opaque, false },
            { RHIFillMode::Solid, RHICompareOp::Always, MaterialBlendMode::Additive, true },
        };
        for (const auto& variant : view_mode_variants)
        {
            pipeline_desc = {};
            pipeline_desc.vertex_shader = GetShader(ShaderId::VSTerrainCommon);
            pipeline_desc.pixel_shader = GetShader(ShaderId::PSTerrainUnlit);
            pipeline_desc.sample_count = sample_count;
            pipeline_desc.depth_stencil_format = dsv_format;
            pipeline_desc.depth_stencil.depth_test = true;
            pipeline_desc.depth_stencil.depth_write = false;
            pipeline_desc.depth_stencil.depth_compare = variant.depth_compare;
            pipeline_desc.blend.enable = variant.blend_enabled;
            pipeline_desc.blend.mode = RHIBlendMode::Additive;
            pipeline_desc.raster.fill_mode = variant.fill_mode;
            pipeline_desc.render_target_formats = { hdr_rtv_format };
            for (uint32 cull_index = 0; cull_index < 2; ++cull_index)
            {
                const RHICullMode cull_mode = cull_index == 0 ? RHICullMode::Back : RHICullMode::None;
                pipeline_desc.raster.cull_mode = cull_mode;
                pipeline_hash = {};
                pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
                pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(cull_mode);
                pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(variant.fill_mode);
                pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(variant.depth_compare);
                pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_UNLIT;
                pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(variant.blend_mode);
                pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(ShaderId::VSTerrainCommon);
                pipeline_hash.storage.bits.pixel_shader = static_cast<uint64>(ShaderId::PSTerrainUnlit);
                graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            }
        }

        const struct
        {
            MaterialBlendMode blend_mode;
            bool masked;
            RHIBlendMode rhi_mode;
        } foliage_variants[] = {
            { MaterialBlendMode::Opaque, false, RHIBlendMode::Alpha },
            { MaterialBlendMode::Masked, true, RHIBlendMode::Alpha },
            { MaterialBlendMode::Transparent, false, RHIBlendMode::Alpha },
            { MaterialBlendMode::Additive, false, RHIBlendMode::Additive },
            { MaterialBlendMode::Premultiplied, false, RHIBlendMode::Premultiplied },
        };
        for (const auto& foliage_variant : foliage_variants)
        {
            for (uint32 clustered = 0; clustered < 2; ++clustered)
            {
                const ShaderId pixel_shader = foliage_variant.masked
                    ? (clustered ? ShaderId::PSObjectForwardPlusMasked : ShaderId::PSObjectForwardMasked)
                    : (clustered ? ShaderId::PSObjectForwardPlus : ShaderId::PSObjectForward);

                pipeline_desc = {};
                const bool blended = foliage_variant.blend_mode >= MaterialBlendMode::Transparent;
                pipeline_desc.vertex_shader = GetShader(ShaderId::VSFoliageCommon);
                pipeline_desc.pixel_shader = GetShader(pixel_shader);
                pipeline_desc.sample_count = sample_count;
                pipeline_desc.depth_stencil_format = dsv_format;
                pipeline_desc.depth_stencil.depth_test = true;
                pipeline_desc.depth_stencil.depth_write = !blended;
                pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
                pipeline_desc.blend.enable = blended;
                pipeline_desc.blend.mode = foliage_variant.rhi_mode;
                pipeline_desc.render_target_formats = { hdr_rtv_format };

                for (uint32 cull_index = 0; cull_index < 2; ++cull_index)
                {
                    const RHICullMode cull_mode = cull_index == 0 ? RHICullMode::Back : RHICullMode::None;
                    pipeline_desc.raster.cull_mode = cull_mode;
                    pipeline_hash = {};
                    pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
                    pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                    pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(cull_mode);
                    pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                    pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(pipeline_desc.depth_stencil.depth_compare);
                    pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_PBR;
                    pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(foliage_variant.blend_mode);
                    pipeline_hash.storage.bits.clustered = clustered;
                    pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(ShaderId::VSFoliageCommon);
                    graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
                }
            }
        }

        for (const auto& variant : view_mode_variants)
        {
            pipeline_desc = {};
            pipeline_desc.vertex_shader = GetShader(ShaderId::VSFoliageSimple);
            pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlit);
            pipeline_desc.sample_count = sample_count;
            pipeline_desc.depth_stencil_format = dsv_format;
            pipeline_desc.depth_stencil.depth_test = true;
            pipeline_desc.depth_stencil.depth_write = false;
            pipeline_desc.depth_stencil.depth_compare = variant.depth_compare;
            pipeline_desc.blend.enable = variant.blend_enabled;
            pipeline_desc.blend.mode = RHIBlendMode::Additive;
            pipeline_desc.raster.fill_mode = variant.fill_mode;
            pipeline_desc.render_target_formats = { hdr_rtv_format };
            for (uint32 cull_index = 0; cull_index < 2; ++cull_index)
            {
                const RHICullMode cull_mode = cull_index == 0 ? RHICullMode::Back : RHICullMode::None;
                pipeline_desc.raster.cull_mode = cull_mode;
                pipeline_hash = {};
                pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
                pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(cull_mode);
                pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(variant.fill_mode);
                pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(variant.depth_compare);
                pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_UNLIT;
                pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(variant.blend_mode);
                pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(ShaderId::VSFoliageSimple);
                graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            }
        }

        const struct
        {
            PrepassMode prepass_mode;
            MaterialBlendMode blend_mode;
            ShaderId vertex_shader;
            ShaderId pixel_shader;
            uint32 render_target_count;
        } foliage_prepass_variants[] = {
            { PrepassMode::DepthOnly, MaterialBlendMode::Opaque, ShaderId::VSFoliagePrepass, ShaderId::Count, 0 },
            { PrepassMode::Normal, MaterialBlendMode::Opaque, ShaderId::VSFoliageNormal, ShaderId::PSObjectNormal, 1 },
            { PrepassMode::Motion, MaterialBlendMode::Opaque, ShaderId::VSFoliageMotion, ShaderId::PSObjectMotion, 1 },
            { PrepassMode::Motion, MaterialBlendMode::Masked, ShaderId::VSFoliageMotionMasked, ShaderId::PSObjectMotionMasked, 1 },
            { PrepassMode::MotionNormal, MaterialBlendMode::Opaque, ShaderId::VSFoliageMotionNormal, ShaderId::PSObjectMotionNormal, 2 },
            { PrepassMode::MotionNormal, MaterialBlendMode::Masked, ShaderId::VSFoliageMotionNormalMasked, ShaderId::PSObjectMotionNormalMasked, 2 },
        };
        for (const auto& foliage_prepass_variant : foliage_prepass_variants)
        {
            pipeline_desc = {};
            pipeline_desc.vertex_shader = GetShader(foliage_prepass_variant.vertex_shader);
            pipeline_desc.pixel_shader = foliage_prepass_variant.pixel_shader == ShaderId::Count ? nullptr : GetShader(foliage_prepass_variant.pixel_shader);
            pipeline_desc.sample_count = sample_count;
            pipeline_desc.depth_stencil_format = dsv_format;
            pipeline_desc.depth_stencil.depth_test = true;
            pipeline_desc.depth_stencil.depth_write = true;
            pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
            pipeline_desc.blend.enable = false;
            pipeline_desc.render_target_formats.assign(foliage_prepass_variant.render_target_count, RHIFormat::R16G16B16A16Float);

            for (uint32 cull_index = 0; cull_index < 2; ++cull_index)
            {
                const RHICullMode cull_mode = cull_index == 0 ? RHICullMode::Back : RHICullMode::None;
                pipeline_desc.raster.cull_mode = cull_mode;
                pipeline_hash = {};
                pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Prepass);
                pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(foliage_prepass_variant.prepass_mode);
                pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
                pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(cull_mode);
                pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
                pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
                pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(foliage_prepass_variant.blend_mode);
                pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(foliage_prepass_variant.vertex_shader);
                graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            }
        }

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectSimple);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectUnlit);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = true;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::LineList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::PrimitivePass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::LineList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.topology = RHIPrimitiveTopology::PointList;
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::PointList);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSSprite2D);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSSprite);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::Always;
        pipeline_desc.blend.enable = true;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite2DPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
        pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite2DPassMode::Sprite);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.pixel_shader = GetShader(ShaderId::PSText3D);
        pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite2DPassMode::Text);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSSprite3D);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSSprite);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = true;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Sprite3DPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);

        const struct { RHIBlendMode mode; bool blend_enable; ShaderId pixel_shader; uint64 hash_bits; } sprite_blend_variants[] = {
            { RHIBlendMode::Alpha, false, ShaderId::PSSprite, static_cast<uint64>(MaterialBlendMode::Opaque) },
            { RHIBlendMode::Alpha, false, ShaderId::PSSpriteMasked, static_cast<uint64>(MaterialBlendMode::Masked) },
            { RHIBlendMode::Alpha, true, ShaderId::PSSprite, static_cast<uint64>(MaterialBlendMode::Transparent) },
            { RHIBlendMode::Additive, true, ShaderId::PSSprite, static_cast<uint64>(MaterialBlendMode::Additive) },
            { RHIBlendMode::Premultiplied, true, ShaderId::PSSprite, static_cast<uint64>(MaterialBlendMode::Premultiplied) },
        };

        for (const auto& variant : sprite_blend_variants)
        {
            pipeline_desc.blend.enable = variant.blend_enable;
            pipeline_desc.blend.mode = variant.mode;
            pipeline_hash.storage.bits.blend_mode = variant.hash_bits;

            pipeline_desc.vertex_shader = GetShader(ShaderId::VSSprite3D);
            pipeline_desc.pixel_shader = GetShader(variant.pixel_shader);
            pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite3DPassMode::Sprite);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

            pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite3DPassMode::Particle);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        }

        pipeline_desc.blend.enable = true;
        pipeline_desc.blend.mode = RHIBlendMode::Alpha;
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSSprite3D);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSText3D);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Transparent);
        pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite3DPassMode::Text);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSDecal);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSDecal);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.blend.enable = true;
        pipeline_desc.blend.mode = RHIBlendMode::Alpha;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::DecalPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Transparent);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSWaterInfo);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSWaterInfo);
        pipeline_desc.sample_count = 1;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { RHIFormat::R32G32Float };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::WaterInfoPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Opaque);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSWaterRippleSplat);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSWaterRippleSplat);
        pipeline_desc.sample_count = 1;
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::Always;
        pipeline_desc.blend.enable = true;
        pipeline_desc.blend.mode = RHIBlendMode::Additive;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { RHIFormat::R32Float };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::WaterRippleSplatPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Transparent);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSWater);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::WaterPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Opaque);
        for (uint32 wireframe = 0; wireframe < 2; ++wireframe)
        {
            const RHIFillMode water_fill_mode = wireframe ? RHIFillMode::Wireframe : RHIFillMode::Solid;
            pipeline_desc.raster.fill_mode = water_fill_mode;
            pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(water_fill_mode);
            for (uint32 clustered = 0; clustered < 2; ++clustered)
            {
                pipeline_desc.pixel_shader = GetShader(clustered ? ShaderId::PSWaterForwardPlus : ShaderId::PSWaterForward);
                pipeline_hash.storage.bits.clustered = clustered;
                graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
            }
        }
        pipeline_desc.raster.fill_mode = RHIFillMode::Solid;

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSFullTriangle);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSComposite);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { ldr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::CompositePass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSGrid);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSGrid);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = true;
        pipeline_desc.blend.mode = RHIBlendMode::Alpha;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::GridPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Transparent);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

#ifndef WON_SHIPPING
        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSDebugDraw2D);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSDebugDraw2D);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.blend.enable = true;
        pipeline_desc.blend.mode = RHIBlendMode::Alpha;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { ldr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::DebugDraw2DPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Transparent);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSDebugDraw3D);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSDebugDraw3D);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::LineList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::DebugDraw3DPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::LineList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
#endif

        for (Size shader_index = 0; shader_index < shaders.size(); ++shader_index)
        {
            rendering::RHIShader* shader = shaders[shader_index].get();
            if (!shader || shader->GetStage() != RHIShaderStage::Compute)
            {
                continue;
            }

            const ShaderId shader_id = static_cast<ShaderId>(shader_index);
            rendering::RHIComputePipelineDesc compute_pipeline_desc = {};
            compute_pipeline_desc.compute_shader = shader;
            std::shared_ptr<rendering::RHIPipeline> compute_pipeline = device->CreateComputePipeline(compute_pipeline_desc);
            if (!compute_pipeline)
            {
                backlog::Post(String("failed to create compute pipeline: ") + ToString(shader_id), backlog::LogLevel::Error);
                return false;
            }

            compute_pipeline->SetName(ToString(shader_id));
            compute_pipeline_cache[ComputePipelineHash(shader_id).storage.value] = compute_pipeline;
        }

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSImpostor);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSImpostorForward);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = true;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = false;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.vertex_shader = static_cast<uint64>(ShaderId::VSImpostor);
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

        pipeline_desc.pixel_shader = GetShader(ShaderId::PSImpostorForwardPlus);
        pipeline_hash.storage.bits.clustered = 1;
        graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        pipeline_hash.storage.bits.clustered = 0;

        const struct
        {
            PrepassMode prepass_mode;
            ShaderId pixel_shader;
            uint32 render_target_count;
        } impostor_prepass_variants[] = {
            { PrepassMode::DepthOnly, ShaderId::PSImpostorPrepass, 0 },
            { PrepassMode::Normal, ShaderId::PSImpostorNormal, 1 },
            { PrepassMode::Motion, ShaderId::PSImpostorMotion, 1 },
            { PrepassMode::MotionNormal, ShaderId::PSImpostorMotionNormal, 2 },
        };
        for (const auto& impostor_prepass_variant : impostor_prepass_variants)
        {
            pipeline_desc.pixel_shader = GetShader(impostor_prepass_variant.pixel_shader);
            pipeline_desc.render_target_formats.assign(impostor_prepass_variant.render_target_count, RHIFormat::R16G16B16A16Float);
            pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::Prepass);
            pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(impostor_prepass_variant.prepass_mode);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        }

        pipeline_desc.pixel_shader = GetShader(ShaderId::PSImpostorForward);
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
        pipeline_hash.storage.bits.pass_mode = 0;
        for (const auto& variant : view_mode_variants)
        {
            pipeline_desc.depth_stencil.depth_write = false;
            pipeline_desc.depth_stencil.depth_compare = variant.depth_compare;
            pipeline_desc.blend.enable = variant.blend_enabled;
            pipeline_desc.blend.mode = RHIBlendMode::Additive;
            pipeline_desc.raster.fill_mode = variant.fill_mode;
            pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(variant.fill_mode);
            pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(variant.depth_compare);
            pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(variant.blend_mode);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        }

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

    rendering::RHIShader* ShaderLibrary::GetShader(ShaderId shader_id) const
    {
        if (shader_id == ShaderId::Count)
        {
            return nullptr;
        }
        return shaders[ToIndex(shader_id)].get();
    }

    rendering::RHIPipeline* ShaderLibrary::GetPipeline(GraphicsPipelineHash pipeline_hash) const
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

        return it->second.get();
    }

    rendering::RHIPipeline* ShaderLibrary::GetPipeline(ComputePipelineHash pipeline_hash) const
    {
        auto it = compute_pipeline_cache.find(pipeline_hash.storage.value);
        if (it == compute_pipeline_cache.end())
        {
            return nullptr;
        }

        return it->second.get();
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

}
