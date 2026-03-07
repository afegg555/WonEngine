#include "ShaderCompiler.h"
#include "ShaderLibrary.h"
#include "JobSystem.h"
#include "Backlog.h"

using namespace won;

int main(int argc, char** argv)
{
    backlog::SetLogFile("ShaderOfflineCompileLog.log");
    jobsystem::Initialize();

    resource::ShaderCompilerOptions compiler_options;
    compiler_options.backend = resource::ShaderCompilerBackend::DXC;
    resource::ShaderLibrary shader_library(compiler_options);
    shader_library.LoadAllShaders();

    return 0;
}
