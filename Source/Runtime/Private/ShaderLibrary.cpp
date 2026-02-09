#include "ShaderLibrary.h"
#include "Backlog.h"

using namespace won::rendering;

namespace won::resource
{
    ShaderLibrary::ShaderLibrary(const ShaderCompilerOptions& options)
        : compiler_options(options), shader_compiler(CreateShaderCompiler(options)), shaders(static_cast<Size>(ShaderId::Count))
    {
    }

    bool ShaderLibrary::LoadAllShaders()
    {
        if (!LoadShader(ShaderId::TestTriangleVS, RHIShaderStage::Vertex, "TestTriangleVS.hlsl"))
        {
            return false;
        }
        if (!LoadShader(ShaderId::TestRedPS, RHIShaderStage::Pixel, "TestRedPS.hlsl"))
        {
            return false;
        }
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
            String log = "Shader compilation failed";
            if (!desc.source_path.empty())
            {
                log += ": ";
                log += desc.source_path;
            }
            backlog::Post(log, backlog::LogLevel::Error);
            return false;
        }

        auto shader = std::make_shared<RHIShader>(desc.stage, shader_bytecode.bytecode.data(), shader_bytecode.bytecode.size());
        shader->SetName(ToName(shader_id));
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

    void ShaderLibrary::Clear()
    {
        for (auto& shader : shaders)
        {
            shader = nullptr;
        }
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

    Size ShaderLibrary::ToIndex(ShaderId shader_id)
    {
        return static_cast<Size>(shader_id);
    }

    const char* ShaderLibrary::ToName(ShaderId shader_id)
    {
        switch (shader_id)
        {
        case ShaderId::TestTriangleVS: return "TestTriangleVS";
        case ShaderId::TestRedPS: return "TestRedPS";
        default: return "UnknownShader";
        }
    }
}
