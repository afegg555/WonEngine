#include "ShaderLoader.h"
#include "ShaderCompiler.h"
#include "Backlog.h"
#include "BinaryArchive.h"
#include "JsonArchive.h"
#include "FileSystem.h"
#include "ResourceExtension.h"

using namespace won::serialize;

namespace won::resource::shaderloader
{
    namespace
    {
        bool IsShaderOutdated(String binary_file_name)
        {
            String dependency_path = io::ReplaceExtension(binary_file_name, shader_dependency_extension);

            if (!io::Exists(binary_file_name))
            {
                return true;
            }
            if (!io::Exists(dependency_path))
            {
                return false;
            }

            uint64 timestamp = 0ull;
            io::GetLastTimestamp(binary_file_name, &timestamp);

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

        bool SaveShaderCompileResult(const ShaderCompileResult& result, const String& binary_file_name)
        {
            String dependency_path = io::ReplaceExtension(binary_file_name, shader_dependency_extension);

            {
                BinaryArchive archive(dependency_path, ArchiveMode::Write);
                Serialize(archive, result.dependencies);
            }

            if (!io::WriteAllBytes(binary_file_name, result.bytecode.data(), result.bytecode.size()))
            {
                return false;
            }

            return true;
        }

        String GetShaderBinaryPath(const String& bin_root, const ShaderCompileDesc& desc)
        {
            String file_name = desc.source_file_name;
            if (!desc.entry_point.empty() && desc.entry_point != "main")
            {
                file_name = io::ReplaceExtension(file_name, desc.entry_point + "." + io::GetExtension(file_name));
            }
            file_name = io::ReplaceExtension(file_name, shader_binary_extension);
            if (bin_root.empty())
            {
                return file_name;
            }
            return bin_root + "/" + file_name;
        }
    }

    bool DumpShaderMetadata(const String& bin_root, const String& out_file_path)
    {
        const ShaderManifest& manifest = GetDefaultShaderManifest();

        JsonArchive archive(ArchiveMode::Write, { true });
        archive.BeginArray();
        for (const ShaderManifestEntry& entry : manifest)
        {
            const String binary_path = GetShaderBinaryPath(bin_root, entry.compile_desc);
            const String dependency_path = io::ReplaceExtension(binary_path, shader_dependency_extension);

            archive.BeginItem();
            archive.BeginObject();
            archive.Field("shader_id", String(ToString(entry.shader_id)));
            archive.Field("source_file", String(entry.compile_desc.source_file_name));
            archive.Field("entry_point", String(entry.compile_desc.entry_point));
            archive.Field("stage", String(rendering::ToString(entry.compile_desc.stage)));
            archive.Field("format", String(ToString(entry.compile_desc.format)));
            archive.Field("model", String(ToString(entry.compile_desc.model)));
            archive.Field("binary_path", binary_path);
            archive.Field("dependency_path", dependency_path);
            archive.EndObject();
            archive.EndItem();
        }
        archive.EndArray();

        if (archive.HasError() || !archive.SaveToFile(out_file_path))
        {
            backlog::Post("Failed to write shader metadata: " + out_file_path, backlog::LogLevel::Error);
            return false;
        }
        backlog::Post("Shader metadata written: " + out_file_path);
        return true;
    }

    bool LoadShader(const std::unique_ptr<ShaderCompiler>& shader_compiler, const ShaderManifestEntry& entry, std::shared_ptr<rendering::RHIShader>& out_shader)
    {
        if (entry.shader_id >= ShaderId::Count)
        {
            backlog::Post("Invalid shader id", backlog::LogLevel::Error);
            return false;
        }

        out_shader.reset();

        const ShaderCompileDesc& desc = entry.compile_desc;
        const String binary_file_name = GetShaderBinaryPath(shader_compiler->GetCompileOptions().shader_bin_root_path, desc);

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
