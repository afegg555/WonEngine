#include "ShaderCompiler.h"
#include "ShaderLoader.h"
#include "JobSystem.h"
#include "Backlog.h"
#include "RHIShader.h"
#include "FileSystem.h"

#include <atomic>
#include <cstdio>

using namespace won;
using namespace won::rendering;
using namespace won::resource;

int main(int argc, char** argv)
{
    backlog::SetLogFile("ShaderOfflineCompileLog.log");
    jobsystem::Initialize();

    resource::ShaderCompilerOptions compiler_options;
    compiler_options.backend = resource::ShaderCompilerBackend::DXC;
    if (argc > 1)
    {
        compiler_options.shader_bin_root_path = argv[1];
        io::CreateDirectories(compiler_options.shader_bin_root_path);
    }
    if (argc > 2)
    {
        compiler_options.shader_source_root_path = argv[2];
    }
    if (argc > 3)
    {
        std::printf("Too many arguments.\nUsage: ShaderOfflineCompiler [shader_bin_root_path] [shader_source_root_path]\n");
        jobsystem::ShutDown();
        return -1;
    }

    std::shared_ptr<resource::ShaderCompiler> shader_compiler = resource::CreateShaderCompiler(compiler_options);
    if (!shader_compiler)
    {
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
    jobsystem::ShutDown();

    return load_succeeded.load() ? 0 : -1;
}
