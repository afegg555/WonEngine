#include "ShaderCompiler.h"
#include "ShaderLoader.h"
#include "JobSystem.h"
#include "Backlog.h"
#include "RHIShader.h"

using namespace won;
using namespace won::rendering;
using namespace won::resource;

int main(int argc, char** argv)
{
    backlog::SetLogFile("ShaderOfflineCompileLog.log");
    jobsystem::Initialize();

    resource::ShaderCompilerOptions compiler_options;
    compiler_options.backend = resource::ShaderCompilerBackend::DXC;
    std::shared_ptr<resource::ShaderCompiler> shader_compiler = resource::CreateShaderCompiler(compiler_options);
    if (!shader_compiler)
    {
        return -1;
    }
    const resource::ShaderManifest& manifest = resource::GetDefaultShaderManifest();

    jobsystem::Context ctx;
    for (const auto& entry : manifest)
    {
        jobsystem::Execute(ctx, [&shader_compiler, entry](jobsystem::JobArgs args) {
            std::shared_ptr<RHIShader> shader;
            shaderloader::LoadShader(shader_compiler, entry, shader);
            });
    }

    jobsystem::Wait(ctx);
    jobsystem::ShutDown();

    return 0;
}
