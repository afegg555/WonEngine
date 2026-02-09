#include "DXCShaderCompiler.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "StringUtils.h"
#include <cstring>
#include <filesystem>

#if defined(_WIN32)
#include <dxcapi.h>
#endif

namespace won::resource
{
    namespace
    {
        const char* GetShaderModelSuffix(ShaderModel model)
        {
            switch (model)
            {
            case ShaderModel::SM_6_0: return "6_0";
            case ShaderModel::SM_6_1: return "6_1";
            case ShaderModel::SM_6_2: return "6_2";
            case ShaderModel::SM_6_3: return "6_3";
            case ShaderModel::SM_6_4: return "6_4";
            case ShaderModel::SM_6_5: return "6_5";
            case ShaderModel::SM_6_6: return "6_6";
            case ShaderModel::SM_6_7: return "6_7";
            default: return nullptr;
            }
        }

        const char* GetShaderStagePrefix(rendering::RHIShaderStage stage)
        {
            switch (stage)
            {
            case rendering::RHIShaderStage::Vertex: return "vs";
            case rendering::RHIShaderStage::Pixel: return "ps";
            case rendering::RHIShaderStage::Compute: return "cs";
            default: return nullptr;
            }
        }

        String GetDefaultTargetProfile(const ShaderCompileDesc& desc)
        {
            if (desc.format != ShaderFormat::HLSL6)
            {
                return {};
            }

            const char* stage_prefix = GetShaderStagePrefix(desc.stage);
            const char* model_suffix = GetShaderModelSuffix(desc.model);
            if (!stage_prefix || !model_suffix)
            {
                return {};
            }

            String target_profile = stage_prefix;
            target_profile += "_";
            target_profile += model_suffix;
            return target_profile;
        }
    }

    DXCShaderCompiler::DXCShaderCompiler(const ShaderCompilerOptions& options)
        : compiler_options(options)
    {
#if defined(_WIN32)
        if (FAILED(DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxc_utils))))
        {
            backlog::Post("Failed to create DXC utils", backlog::LogLevel::Error);
            dxc_utils.Reset();
        }

        if (FAILED(DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxc_compiler))))
        {
            backlog::Post("Failed to create DXC compiler", backlog::LogLevel::Error);
            dxc_compiler.Reset();
        }
#endif
    }

    ShaderBytecode DXCShaderCompiler::Compile(const ShaderCompileDesc& desc) const
    {
        ShaderBytecode shader_bytecode = {};
        io::FileData source_file = {};
        if (!desc.source_path.empty())
        {
            std::filesystem::path source_fs_path = std::filesystem::u8path(desc.source_path);
            if (!source_fs_path.is_absolute() && !compiler_options.shader_source_root_path.empty())
            {
                source_fs_path = std::filesystem::u8path(compiler_options.shader_source_root_path) / source_fs_path;
            }
            const String resolved_source_path = source_fs_path.lexically_normal().u8string();
            if (!io::ReadAllBytes(resolved_source_path, &source_file))
            {
                backlog::Post("Failed to load shader source: " + resolved_source_path, backlog::LogLevel::Error);
                return shader_bytecode;
            }
        }

        if (source_file.bytes.empty())
        {
            backlog::Post("Shader source is empty", backlog::LogLevel::Error);
            return shader_bytecode;
        }

        const String entry_point = desc.entry_point.empty() ? "main" : desc.entry_point;
        const String target_profile = GetDefaultTargetProfile(desc);

        if (target_profile.empty())
        {
            backlog::Post("Shader target profile is empty", backlog::LogLevel::Error);
            return shader_bytecode;
        }

#if !defined(_WIN32)
        backlog::Post("DXC shader compiler is only available on Windows", backlog::LogLevel::Error);
        return shader_bytecode;
#else
        if (!dxc_utils || !dxc_compiler)
        {
            backlog::Post("DXC compiler is not initialized", backlog::LogLevel::Error);
            return shader_bytecode;
        }

        DxcBuffer source_buffer = {};
        source_buffer.Ptr = source_file.bytes.data();
        source_buffer.Size = source_file.bytes.size();
        source_buffer.Encoding = DXC_CP_UTF8;

        const WString entry_point_w = utils::ToWideString(entry_point);
        const WString target_profile_w = utils::ToWideString(target_profile);

        LPCWSTR arguments[] =
        {
            L"-E", entry_point_w.c_str(),
            L"-T", target_profile_w.c_str(),
            //L"-rootsig-define", L"DEFAULT_ROOTSIGNATURE",
        };

        ComPtr<IDxcResult> compile_result;
        if (FAILED(dxc_compiler->Compile(&source_buffer, arguments,
            static_cast<uint32>(arraysize(arguments)), nullptr, IID_PPV_ARGS(&compile_result))))
        {
            backlog::Post("DXC compile call failed", backlog::LogLevel::Error);
            return shader_bytecode;
        }

        ComPtr<IDxcBlobUtf8> errors;
        if (SUCCEEDED(compile_result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) &&
            errors && errors->GetStringLength() > 0)
        {
            backlog::Post(errors->GetStringPointer(), backlog::LogLevel::Error);
        }

        HRESULT compile_status = E_FAIL;
        if (FAILED(compile_result->GetStatus(&compile_status)) || FAILED(compile_status))
        {
            backlog::Post("DXC failed to compile shader", backlog::LogLevel::Error);
            return shader_bytecode;
        }

        ComPtr<IDxcBlob> object_blob;
        if (FAILED(compile_result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object_blob), nullptr)) || !object_blob)
        {
            backlog::Post("DXC did not return shader bytecode", backlog::LogLevel::Error);
            return shader_bytecode;
        }

        shader_bytecode.bytecode.resize(object_blob->GetBufferSize());
        if (!shader_bytecode.bytecode.empty())
        {
            std::memcpy(shader_bytecode.bytecode.data(), object_blob->GetBufferPointer(),
                object_blob->GetBufferSize());
        }

        return shader_bytecode;
#endif
    }
}
