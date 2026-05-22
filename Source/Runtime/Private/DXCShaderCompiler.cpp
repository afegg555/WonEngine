#include "DXCShaderCompiler.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "StringUtils.h"
#include <cstring>

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
        : ShaderCompiler(options)
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

    ShaderCompileResult DXCShaderCompiler::Compile(const ShaderCompileDesc& desc) const
    {
        ShaderCompileResult compile_result = {};
        io::FileData source_file = {};
        if (!desc.source_file_name.empty())
        {
            String resolved_source_path = desc.source_file_name;
            if (!io::IsAbsolutePath(resolved_source_path) && !compiler_options.shader_source_root_path.empty())
            {
                resolved_source_path = io::CombinePath(compiler_options.shader_source_root_path, resolved_source_path);
            }
            else
            {
                resolved_source_path = io::NormalizePath(resolved_source_path);
            }
            if (!io::ReadAllBytes(resolved_source_path, &source_file))
            {
                backlog::Post("Failed to load shader source: " + resolved_source_path, backlog::LogLevel::Error);
                return compile_result;
            }
        }

        if (source_file.bytes.empty())
        {
            return compile_result;
        }

        const String entry_point = desc.entry_point.empty() ? "main" : desc.entry_point;
        const String target_profile = GetDefaultTargetProfile(desc);

#if !defined(_WIN32)
        backlog::Post("DXC shader compiler is only available on Windows", backlog::LogLevel::Error);
        return compile_result;
#else
        if (!dxc_utils || !dxc_compiler)
        {
            backlog::Post("DXC compiler is not initialized", backlog::LogLevel::Error);
            return compile_result;
        }

        DxcBuffer source_buffer = {};
        source_buffer.Ptr = source_file.bytes.data();
        source_buffer.Size = source_file.bytes.size();
        source_buffer.Encoding = DXC_CP_UTF8;

        const WString entry_point_w = utils::DecodeUtf8(entry_point);
        const WString target_profile_w = utils::DecodeUtf8(target_profile);
        const WString shader_source_root_path_w = utils::DecodeUtf8(compiler_options.shader_source_root_path);

        Vector<LPCWSTR> arguments =
        {
            L"-E", entry_point_w.c_str(),
            L"-T", target_profile_w.c_str(),
            L"-rootsig-define", L"DEFAULT_ROOTSIGNATURE",
            L"-Wno-conversion",
        };
        if (!shader_source_root_path_w.empty())
        {
            arguments.push_back(L"-I");
            arguments.push_back(shader_source_root_path_w.c_str());
        }

        struct IncludeHandler : public IDxcIncludeHandler
        {
            ComPtr<IDxcIncludeHandler> dxcIncludeHandler;
            ShaderCompileResult* result;

            HRESULT STDMETHODCALLTYPE LoadSource(
                _In_z_ LPCWSTR pFilename,                                 // Candidate filename.
                _COM_Outptr_result_maybenull_ IDxcBlob** ppIncludeSource  // Resultant source object for included file, nullptr if not found.
            ) override
            {
                HRESULT hr = dxcIncludeHandler->LoadSource(pFilename, ppIncludeSource);
                if (SUCCEEDED(hr))
                {
                    std::string& filename = result->dependencies.emplace_back();
                    filename = won::utils::EncodeUtf8(pFilename);
                }
                return hr;
            }
            HRESULT STDMETHODCALLTYPE QueryInterface(
                /* [in] */ REFIID riid,
                /* [iid_is][out] */ _COM_Outptr_ void __RPC_FAR* __RPC_FAR* ppvObject) override
            {
                return dxcIncludeHandler->QueryInterface(riid, ppvObject);
            }

            ULONG STDMETHODCALLTYPE AddRef(void) override
            {
                return 0;
            }
            ULONG STDMETHODCALLTYPE Release(void) override
            {
                return 0;
            }
        } include_handler;
        include_handler.result = &compile_result;
        dxc_utils->CreateDefaultIncludeHandler(&include_handler.dxcIncludeHandler);

        ComPtr<IDxcResult> result;
        if (FAILED(dxc_compiler->Compile(&source_buffer, arguments.data(),
            static_cast<uint32>(arguments.size()), &include_handler, IID_PPV_ARGS(&result))))
        {
            backlog::Post("DXC compile call failed", backlog::LogLevel::Error);
            return compile_result;
        }

        ComPtr<IDxcBlobUtf8> errors;
        if (SUCCEEDED(result->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&errors), nullptr)) &&
            errors && errors->GetStringLength() > 0)
        {
            backlog::Post(errors->GetStringPointer(), backlog::LogLevel::Error);
        }

        HRESULT compile_status = E_FAIL;
        if (FAILED(result->GetStatus(&compile_status)) || FAILED(compile_status))
        {
            backlog::Post("DXC failed to compile shader", backlog::LogLevel::Error);
            return compile_result;
        }

        ComPtr<IDxcBlob> object_blob;
        if (FAILED(result->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&object_blob), nullptr)) || !object_blob)
        {
            backlog::Post("DXC did not return shader bytecode", backlog::LogLevel::Error);
            return compile_result;
        }

        compile_result.bytecode.resize(object_blob->GetBufferSize());
        std::memcpy(compile_result.bytecode.data(), object_blob->GetBufferPointer(),
            object_blob->GetBufferSize());

        return compile_result;
#endif
    }
}
