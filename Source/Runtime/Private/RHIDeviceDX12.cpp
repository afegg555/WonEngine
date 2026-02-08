#include "RHIDeviceDX12.h"
#include "Platform.h"
#include "Backlog.h"
#include "Types.h"
#include "StringUtils.h"
#include "RHIContextDX12.h"
#include "RHICommandAllocatorDX12.h"
#include "RHICommandListDX12.h"

#include "DirectX-Headers/d3d12.h"
#include "DirectX-Headers/d3dx12_default.h"
#include "DirectX-Headers/d3dx12_check_feature_support.h"

#include "D3D12MemoryAllocator/D3D12MemAlloc.cpp"
#include <dxgi1_6.h>
#ifdef _DEBUG
#include <dxgidebug.h>
#endif

namespace won::rendering
{
    namespace
    {
        const char* FeatureLevelToString(D3D_FEATURE_LEVEL level)
        {
            switch (level)
            {
            case D3D_FEATURE_LEVEL_12_2:
                return "12_2";
            case D3D_FEATURE_LEVEL_12_1:
                return "12_1";
            case D3D_FEATURE_LEVEL_12_0:
                return "12_0";
            case D3D_FEATURE_LEVEL_11_1:
                return "11_1";
            case D3D_FEATURE_LEVEL_11_0:
                return "11_0";
            default:
                return "Unknown";
            }
        }

    }

    RHIDeviceDX12::RHIDeviceDX12(const RHIDeviceDesc& desc)
        : device_desc(desc)
    {

        UINT factory_flags = 0;
#ifdef _DEBUG
        if (desc.enable_debug_layer)
        {
            ComPtr<ID3D12Debug> debug;
            if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debug))) && debug)
            {
                debug->EnableDebugLayer();
                if (desc.enable_gpu_validation)
                {
                    ComPtr<ID3D12Debug1> debug1;
                    if (SUCCEEDED(debug.As(&debug1)) && debug1)
                    {
                        debug1->SetEnableGPUBasedValidation(TRUE);
                    }
                }
            }
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;
        }
#endif
        
        if (FAILED(CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory))))
        {
            backlog::Post("Failed to create dxgi factory", backlog::LogLevel::Error);
            return;
        }

#ifdef _DEBUG
        if (desc.enable_debug_layer)
        {
            ComPtr<IDXGIInfoQueue> dxgi_info_queue;
            if (SUCCEEDED(DXGIGetDebugInterface1(0, IID_PPV_ARGS(dxgi_info_queue.GetAddressOf()))))
            {
                dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, TRUE);
                dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, TRUE);

                dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_WARNING, TRUE);

                //DXGI_INFO_QUEUE_MESSAGE_ID hide[] =
                //{
                //};
                //DXGI_INFO_QUEUE_FILTER filter = {};
                //filter.DenyList.NumIDs = static_cast<UINT>(std::size(hide));
                //filter.DenyList.pIDList = hide;
                //dxgi_info_queue->AddStorageFilterEntries(DXGI_DEBUG_ALL, &filter);
            }

        }
#endif
        D3D_FEATURE_LEVEL min_feature_level = D3D_FEATURE_LEVEL_12_0;

        if (desc.preference == RHIDevicePreference::Software)
        {
            factory->EnumWarpAdapter(IID_PPV_ARGS(&adapter));
        }
        else
        {
            DXGI_GPU_PREFERENCE dxgi_preference;
            switch (desc.preference)
            {
            case RHIDevicePreference::Discrete:
                dxgi_preference = DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE;
                break;
            case RHIDevicePreference::Integrated:
                dxgi_preference = DXGI_GPU_PREFERENCE_MINIMUM_POWER;
                break;
            default:
                dxgi_preference = DXGI_GPU_PREFERENCE_UNSPECIFIED;
                break;
            }

            for (UINT i = 0; factory->EnumAdapterByGpuPreference(i, dxgi_preference,
                     IID_PPV_ARGS(&adapter)) != DXGI_ERROR_NOT_FOUND; ++i)
            {
                DXGI_ADAPTER_DESC1 adapter_desc = {};
                adapter->GetDesc1(&adapter_desc);
                if (adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
                {
                    continue;
                }
                
                if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), min_feature_level, __uuidof(ID3D12Device), nullptr)))
                {
                    break;
                }

                adapter.Reset();
            }
        }

        if (adapter)
        {
            D3D12CreateDevice(adapter.Get(), min_feature_level, IID_PPV_ARGS(&device));
        }

        if (!device)
        {
            backlog::Post("Failed to create device", backlog::LogLevel::Error);
            return;
        }

        // https://devblogs.microsoft.com/directx/introducing-a-new-api-for-checking-feature-support-in-direct3d-12/
        CD3DX12FeatureSupport features;
        if (FAILED(features.Init(device.Get())))
        {
            backlog::Post("Failed to check feature support", backlog::LogLevel::Error);
            return;
        }

        D3D_FEATURE_LEVEL requested_levels[] =
        {
            D3D_FEATURE_LEVEL_12_2,
            D3D_FEATURE_LEVEL_12_1,
            D3D_FEATURE_LEVEL_12_0,
            D3D_FEATURE_LEVEL_11_1,
            D3D_FEATURE_LEVEL_11_0
        };
        D3D12_FEATURE_DATA_FEATURE_LEVELS feature_levels = {};
        feature_levels.NumFeatureLevels = static_cast<UINT>(sizeof(requested_levels) / sizeof(requested_levels[0]));
        feature_levels.pFeatureLevelsRequested = requested_levels;
        if (FAILED(device->CheckFeatureSupport(D3D12_FEATURE_FEATURE_LEVELS, &feature_levels, sizeof(feature_levels))))
        {
            backlog::Post("Failed to query feature levels", backlog::LogLevel::Warning);
            feature_levels.MaxSupportedFeatureLevel = D3D_FEATURE_LEVEL_12_0;
        }

        DXGI_ADAPTER_DESC1 selected_adapter_desc = {};
        if (adapter)
        {
            adapter->GetDesc1(&selected_adapter_desc);
        }

        const String adapter_name = utils::ToString(selected_adapter_desc.Description);
        if (!adapter_name.empty())
        {
            wonlog("DX12 Adapter: %s", adapter_name.c_str());
        }
        wonlog("DX12 Feature Level: %s", FeatureLevelToString(feature_levels.MaxSupportedFeatureLevel));
#ifdef D3D12_SDK_VERSION
        wonlog("DX12 SDK Version: %u", static_cast<uint32>(D3D12_SDK_VERSION));
#endif
        
        if (features.HighestRootSignatureVersion() < D3D_ROOT_SIGNATURE_VERSION_1_1)
        {
            backlog::Post("Root Signature not supported", backlog::LogLevel::Error);
            return;
        }

        D3D12MA::ALLOCATOR_DESC allocatorDesc = {};
        allocatorDesc.pDevice = device.Get();
        allocatorDesc.pAdapter = adapter.Get();

        if (FAILED(D3D12MA::CreateAllocator(&allocatorDesc, resource_allocator.ReleaseAndGetAddressOf())))
        {
            backlog::Post("Failed to create memory allocator", backlog::LogLevel::Error);
            return;
        }

        graphics_context = std::make_shared<RHIContextDX12>(RHIQueueType::Graphics, device);
        if (!graphics_context->IsValid())
        {
            backlog::Post("Failed to create graphics context", backlog::LogLevel::Error);
            graphics_context.reset();
        }

        compute_context = std::make_shared<RHIContextDX12>(RHIQueueType::Compute, device);
        if (!compute_context->IsValid())
        {
            backlog::Post("Failed to create compute context", backlog::LogLevel::Error);
            compute_context.reset();
        }

        copy_context = std::make_shared<RHIContextDX12>(RHIQueueType::Copy, device);
        if (!copy_context->IsValid())
        {
            backlog::Post("Failed to create copy context", backlog::LogLevel::Error);
            copy_context.reset();
        }
    }

    RHIDeviceDX12::~RHIDeviceDX12()
    {
        device.Reset();
        adapter.Reset();
        factory.Reset();
        resource_allocator.Reset();
    }

    std::shared_ptr<RHIFence> RHIDeviceDX12::CreateFence(uint64 initial_value)
    {
        (void)initial_value;
        return nullptr;
    }

    std::shared_ptr<RHICommandAllocator> RHIDeviceDX12::CreateCommandAllocator(RHIQueueType type)
    {
        return std::make_shared<RHICommandAllocatorDX12>(type, device);
    }

    std::shared_ptr<RHICommandList> RHIDeviceDX12::CreateCommandList(RHIQueueType type)
    {
        return std::make_shared<RHICommandListDX12>(type, device);
    }

    std::shared_ptr<RHIResource> RHIDeviceDX12::CreateBuffer(const RHIBufferDesc& desc,
        const void* initial_data, Size initial_size)
    {
        (void)desc;
        (void)initial_data;
        (void)initial_size;
        return nullptr;
    }

    std::shared_ptr<RHIResource> RHIDeviceDX12::CreateTexture(const RHITextureDesc& desc,
        const void* initial_data, Size initial_size)
    {
        (void)desc;
        (void)initial_data;
        (void)initial_size;
        return nullptr;
    }

    std::shared_ptr<RHIPipeline> RHIDeviceDX12::CreateGraphicsPipeline(
        const RHIGraphicsPipelineDesc& desc)
    {
        (void)desc;
        return nullptr;
    }

    std::shared_ptr<RHIPipeline> RHIDeviceDX12::CreateComputePipeline(
        const RHIComputePipelineDesc& desc)
    {
        (void)desc;
        return nullptr;
    }

    std::shared_ptr<RHISampler> RHIDeviceDX12::CreateSampler(const RHISamplerDesc& desc)
    {
        (void)desc;
        return nullptr;
    }
}
