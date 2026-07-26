#include "ShaderCompiler.h"
#include "ShaderLoader.h"
#include "JobSystem.h"
#include "Backlog.h"
#include "RHIShader.h"
#include "FileSystem.h"
#include "Configuration.h"

#include <atomic>
#include <cstdio>

using namespace won;
using namespace won::rendering;
using namespace won::resource;

int main(int argc, char** argv)
{
    backlog::SetLogFile("ShaderOfflineCompileLog.log");

    config::Configuration arguments;
    arguments.LoadFromCommandLine(argc, argv);

    // ShaderOfflineCompiler [shader_bin_root_path] [shader_source_root_path] [--dump-metadata]
    const bool dump_metadata = arguments.HasKey("--dump-metadata");
    const char* bin_arg = arguments.GetString("0");
    const char* src_arg = arguments.GetString("1");
    if (arguments.GetString("2") != nullptr)
    {
        std::printf("Too many arguments.\nUsage: ShaderOfflineCompiler [shader_bin_root_path] [shader_source_root_path] [--dump-metadata]\n");
        return -1;
    }
    const String bin_path = bin_arg ? bin_arg : "";
    const String src_path = src_arg ? src_arg : "";

    jobsystem::Initialize();

    resource::ShaderCompilerOptions compiler_options;
    compiler_options.backend = resource::ShaderCompilerBackend::DXC;
    if (!bin_path.empty())
    {
        compiler_options.shader_bin_root_path = String(bin_path);
        io::CreateDirectories(compiler_options.shader_bin_root_path);
    }
    if (!src_path.empty())
    {
        compiler_options.shader_source_root_path = String(src_path);
    }

    std::unique_ptr<resource::ShaderCompiler> shader_compiler = resource::CreateShaderCompiler(compiler_options);
    if (!shader_compiler)
    {
        jobsystem::ShutDown();
        return -1;
    }
    const resource::ShaderManifest& manifest = resource::GetDefaultShaderManifest();

    jobsystem::Context ctx;
    std::atomic_bool load_succeeded = true;
    for (const auto& entry : manifest)
    {
        jobsystem::Execute(ctx, [&shader_compiler, &load_succeeded, entry](jobsystem::JobArgs args) {
            std::shared_ptr<RHIShader> shader;
            if (!shaderloader::LoadShader(shader_compiler, entry, shader))
            {
                load_succeeded.store(false);
            }
        });
    }

    jobsystem::Wait(ctx);

    // Add-on: dump metadata for the binaries that were just compiled, using the
    // resolved bin root so binary_path entries match the actual outputs.
    bool dump_succeeded = true;
    if (dump_metadata)
    {
        const String& resolved_root = shader_compiler->GetCompileOptions().shader_bin_root_path;
        const String out_path = resolved_root.empty() ? String("shader_metadata.json") : (resolved_root + "/shader_metadata.json");
        dump_succeeded = shaderloader::DumpShaderMetadata(resolved_root, out_path);
    }

    jobsystem::ShutDown();

    return (load_succeeded.load() && dump_succeeded) ? 0 : -1;
}
