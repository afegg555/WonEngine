#include "ShaderLoader.h"
#include "ShaderCompiler.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "Serializer.h"

using namespace won::serialize;

namespace won::resource::shaderloader
{
    namespace
    {
        bool IsShaderOutdated(String cso_name)
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
                //String root = io::GetDirectoryFromPath(dependency_path);
                Vector<String> dependencies;
                Serialize(archive, dependencies);
                for (auto& dep : dependencies)
                {
                    std::string dependency = dep;
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

        bool SaveShaderCompileResult(const ShaderCompileResult& result, const String& cso_name)
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
    }

    bool LoadShader(const std::shared_ptr<ShaderCompiler>& shader_compiler, const ShaderManifestEntry& entry, std::shared_ptr<rendering::RHIShader>& out_shader)
    {
        if (entry.shader_id >= ShaderId::Count)
        {
            backlog::Post("Invalid shader id", backlog::LogLevel::Error);
            return false;
        }

        out_shader.reset();

        const ShaderCompileDesc& desc = entry.compile_desc;
        String binary_file_name = shader_compiler->GetCompileOptions().shader_bin_root_path + "/" + io::ReplaceExtension(desc.source_file_name, "cso");

        Vector<uint8> bytecode;
        if (IsShaderOutdated(binary_file_name))
        {
            ShaderCompileResult compile_result = shader_compiler->Compile(desc);
            if (compile_result.bytecode.empty())
            {
                String log = "Shader compilation failed : ";
                if (!desc.source_file_name.empty())
                {
                    log += desc.source_file_name;
                }
                backlog::Post(log, backlog::LogLevel::Error);
                return false;
            }
            String log = "Shader compiled : ";
            log += desc.source_file_name;
            backlog::Post(log);

            String full_source_path = shader_compiler->GetCompileOptions().shader_source_root_path + "/" + desc.source_file_name;
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

        out_shader = std::make_shared<rendering::RHIShader>(desc.stage, bytecode.data(), bytecode.size());
        return out_shader != nullptr;
    }
}
