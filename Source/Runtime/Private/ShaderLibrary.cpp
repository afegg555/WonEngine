#include "ShaderLibrary.h"
#include "Backlog.h"
#include "JobSystem.h"
#include "FileSystem.h"
#include "Serializer.h"

using namespace won::rendering;
using namespace won::serialize;

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

        String binary_file_name = shader_compiler->GetCompileOptions().shader_bin_root_path + "/" + io::ReplaceExtension(desc.source_path, "cso");

        Vector<uint8> bytecode;
        if (IsShaderOutdated(binary_file_name))
        {
            ShaderCompileResult compile_result = shader_compiler->Compile(desc);
            if (compile_result.bytecode.empty())
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

            String full_source_path = shader_compiler->GetCompileOptions().shader_source_root_path + "/" + desc.source_path;
            compile_result.dependencies.push_back(full_source_path);

            SaveShaderCompileResult(compile_result, binary_file_name);

            bytecode = std::move(compile_result.bytecode);
        }
        else
        {
            io::FileData data;
            if (!io::ReadAllBytes(binary_file_name, &data))
            {
                return false;
            }
            bytecode = std::move(data.bytes);
        }

        auto shader = std::make_shared<RHIShader>(desc.stage, bytecode.data(), bytecode.size());
        shaders[ToIndex(shader_id)] = shader;
        return true;
    }

    bool ShaderLibrary::IsShaderOutdated(String cso_name) const
    {
        String dependency_path = io::ReplaceExtension(cso_name, "dep");

        if (!io::Exists(cso_name) || !io::Exists(dependency_path))
        {
            return true;
        }
        
        uint64 timestamp = 0ull;
        io::GetLastTimestamp(cso_name, &timestamp);

        BinaryArchive archive(dependency_path, ArchiveMode::Read);
        if (!archive.IsEnd())
        {
            String root = io::GetDirectoryFromPath(dependency_path);
            Vector<String> dependencies;
            Serialize(archive, dependencies);
            for (auto& dep : dependencies)
            {
                std::string dependency = root + dep;
                if (io::Exists(dependency))
                {
                    uint64 dep_timestamp = 0ull;
                    io::GetLastTimestamp(dependency, &dep_timestamp);
                    if (timestamp < dep_timestamp)
                    {
                        return true;
                    }
                }

            }
        }
        return false;
    }

    bool ShaderLibrary::SaveShaderCompileResult(const ShaderCompileResult& result, const String& cso_name) const
    {
        String dependency_path = io::ReplaceExtension(cso_name, "dep");

        {
            BinaryArchive archive(dependency_path, ArchiveMode::Write);
            Serialize(archive, result.dependencies);
        }

        if (!io::WriteAllBytes(cso_name, result.bytecode.data(), result.bytecode.size()))
        {
            return false;
        }

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
