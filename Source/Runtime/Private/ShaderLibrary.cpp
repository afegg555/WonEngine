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

    bool ShaderLibrary::BuildAllGraphicsPipelines(RHIFormat hdr_rtv_format, RHIFormat ldr_rtv_format, RHIFormat dsv_format, uint32 sample_count)
    {
        if (!device)
        {
            backlog::Post("BuildAllGraphicsPipelines failed: device is null", backlog::LogLevel::Error);
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

        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectCommon);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectForward);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::Always;
        pipeline_desc.blend.enable = true;
        pipeline_desc.blend.mode = RHIBlendMode::Additive;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::Always);
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_PBR;
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Additive);
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

        // Transparent mesh PSOs: blend on, depth write off, GreaterEqual (skip depth prepass)
        pipeline_desc = {};
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSObjectCommon);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSObjectForward);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = dsv_format;
        pipeline_desc.depth_stencil.depth_test = true;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
        pipeline_desc.blend.enable = true;
        pipeline_desc.render_target_formats = { hdr_rtv_format };
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::MainPass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
        pipeline_hash.storage.bits.depth_compare = static_cast<uint64>(RHICompareOp::GreaterEqual);
        pipeline_hash.storage.bits.shader_type = SHADER_MATERIAL_TYPE_PBR;
        pipeline_hash.storage.bits.blend_mode = static_cast<uint64>(MaterialBlendMode::Transparent);
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

        const struct { RHIBlendMode mode; uint64 hash_bits; } sprite_blend_variants[] = {
            { RHIBlendMode::Alpha, static_cast<uint64>(MaterialBlendMode::Transparent) },
            { RHIBlendMode::Additive, static_cast<uint64>(MaterialBlendMode::Additive) },
            { RHIBlendMode::Premultiplied, static_cast<uint64>(MaterialBlendMode::Premultiplied) },
        };

        for (const auto& variant : sprite_blend_variants)
        {
            pipeline_desc.blend.mode = variant.mode;
            pipeline_hash.storage.bits.blend_mode = variant.hash_bits;

            pipeline_desc.vertex_shader = GetShader(ShaderId::VSSprite3D);
            pipeline_desc.pixel_shader = GetShader(ShaderId::PSSprite);
            pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite3DPassMode::Sprite);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);

            pipeline_hash.storage.bits.pass_mode = static_cast<uint64>(Sprite3DPassMode::Particle);
            graphics_pipeline_cache[pipeline_hash.storage.value] = device->CreateGraphicsPipeline(pipeline_desc);
        }

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
        pipeline_desc.vertex_shader = GetShader(ShaderId::VSFullTriangle);
        pipeline_desc.pixel_shader = GetShader(ShaderId::PSComposite);
        pipeline_desc.sample_count = sample_count;
        pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
        pipeline_desc.depth_stencil.depth_test = false;
        pipeline_desc.depth_stencil.depth_write = false;
        pipeline_desc.blend.enable = true;
        pipeline_desc.raster.cull_mode = RHICullMode::None;
        pipeline_desc.render_target_formats = { ldr_rtv_format };
        pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;
        pipeline_hash = {};
        pipeline_hash.storage.bits.render_pass_type = static_cast<uint64>(RenderPassType::CompositePass);
        pipeline_hash.storage.bits.topology = static_cast<uint64>(RHIPrimitiveTopology::TriangleList);
        pipeline_hash.storage.bits.cull_mode = static_cast<uint64>(RHICullMode::None);
        pipeline_hash.storage.bits.fill_mode = static_cast<uint64>(RHIFillMode::Solid);
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

    rendering::RHIPipeline* ShaderLibrary::GetPipeline(ComputePipelineHash pipeline_hash)
    {
        auto it = compute_pipeline_cache.find(pipeline_hash.storage.value);
        if (it != compute_pipeline_cache.end())
        {
            return it->second.get();
        }

        const ShaderId shader_id = static_cast<ShaderId>(pipeline_hash.storage.bits.compute_shader);
        rendering::RHIShader* shader = GetShader(shader_id);
        if (!device || !shader)
        {
            return nullptr;
        }

        rendering::RHIComputePipelineDesc pipeline_desc = {};
        pipeline_desc.compute_shader = shader;
        std::shared_ptr<rendering::RHIPipeline> pipeline = device->CreateComputePipeline(pipeline_desc);
        if (!pipeline)
        {
            return nullptr;
        }

        pipeline->SetName(ToString(shader_id));
        compute_pipeline_cache[pipeline_hash.storage.value] = pipeline;
        return pipeline.get();
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
