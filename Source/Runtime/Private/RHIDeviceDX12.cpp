#include "RHIDeviceDX12.h"
#include "Platform.h"
#include "Backlog.h"
#include "MathUtils.h"
#include "Types.h"
#include "StringUtils.h"
#include "RHIContextDX12.h"
#include "RHICommandAllocatorDX12.h"
#include "RHICommandListDX12.h"
#include "RHIQueryHeapDX12.h"
#include "RHIResourceDX12.h"
#include "RHIPipelineDX12.h"
#include "RHISamplerDX12.h"
#include "RHISwapchainDX12.h"
#include "RHIFormatDX12.h"
#include "DescriptorAllocatorDX12.h"
#include "RHIFenceDX12.h"

#include "DirectX-Headers/d3dx12_default.h"
#include "DirectX-Headers/d3dx12_check_feature_support.h"
#include "DirectX-Headers/d3dx12_resource_helpers.h"
#include "DirectX-Headers/d3dx12_pipeline_state_stream.h"
#include <d3dcompiler.h>

#include "D3D12MemoryAllocator/D3D12MemAlloc.cpp"

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

        Size AlignConstantBufferByteSize(Size size_in_bytes)
        {
            return won::math::Align(size_in_bytes, static_cast<Size>(256u));
        }

        bool GetFormatBytesPerPixel(RHIFormat format, uint32& out_bytes_per_pixel)
        {
            switch (format)
            {
            case RHIFormat::R8Unorm:
            case RHIFormat::R8Uint:
            case RHIFormat::R8Snorm:
            case RHIFormat::R8Sint:
                out_bytes_per_pixel = 1u;
                return true;
            case RHIFormat::R8G8Unorm:
            case RHIFormat::R8G8Uint:
            case RHIFormat::R8G8Snorm:
            case RHIFormat::R8G8Sint:
            case RHIFormat::R16Float:
            case RHIFormat::R16Unorm:
            case RHIFormat::R16Uint:
            case RHIFormat::R16Snorm:
            case RHIFormat::R16Sint:
                out_bytes_per_pixel = 2u;
                return true;
            case RHIFormat::R8G8B8A8Unorm:
            case RHIFormat::R8G8B8A8UnormSrgb:
            case RHIFormat::R8G8B8A8Uint:
            case RHIFormat::R8G8B8A8Snorm:
            case RHIFormat::R8G8B8A8Sint:
            case RHIFormat::B8G8R8A8Unorm:
            case RHIFormat::B8G8R8A8UnormSrgb:
            case RHIFormat::R32Float:
            case RHIFormat::R32Uint:
            case RHIFormat::R32Sint:
            case RHIFormat::R16G16Float:
            case RHIFormat::R16G16Unorm:
            case RHIFormat::R16G16Uint:
            case RHIFormat::R16G16Snorm:
            case RHIFormat::R16G16Sint:
                out_bytes_per_pixel = 4u;
                return true;
            case RHIFormat::R32G32Float:
            case RHIFormat::R32G32Uint:
            case RHIFormat::R32G32Sint:
            case RHIFormat::R16G16B16A16Float:
            case RHIFormat::R16G16B16A16Unorm:
            case RHIFormat::R16G16B16A16Uint:
            case RHIFormat::R16G16B16A16Snorm:
            case RHIFormat::R16G16B16A16Sint:
                out_bytes_per_pixel = 8u;
                return true;
            case RHIFormat::R32G32B32A32Float:
            case RHIFormat::R32G32B32A32Uint:
            case RHIFormat::R32G32B32A32Sint:
                out_bytes_per_pixel = 16u;
                return true;
            default:
                out_bytes_per_pixel = 0u;
                return false;
            }
        }

        void AddDeviceFeature(uint32& feature_flags, RHIDeviceFeature feature)
        {
            feature_flags |= static_cast<uint32>(feature);
        }

        D3D12_COMMAND_LIST_TYPE ToCommandListType(RHIQueueType type)
        {
            switch (type)
            {
            case RHIQueueType::Compute:
                return D3D12_COMMAND_LIST_TYPE_COMPUTE;
            case RHIQueueType::Copy:
                return D3D12_COMMAND_LIST_TYPE_COPY;
            default:
                return D3D12_COMMAND_LIST_TYPE_DIRECT;
            }
        }

        D3D12_PRIMITIVE_TOPOLOGY_TYPE ToTopologyType(RHIPrimitiveTopology topology)
        {
            switch (topology)
            {
            case RHIPrimitiveTopology::PointList:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
            case RHIPrimitiveTopology::LineList:
            case RHIPrimitiveTopology::LineStrip:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
            case RHIPrimitiveTopology::TriangleList:
            case RHIPrimitiveTopology::TriangleStrip:
            default:
                return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            }
        }

        D3D12_FILL_MODE ToFillMode(RHIFillMode fill_mode)
        {
            return fill_mode == RHIFillMode::Wireframe ? D3D12_FILL_MODE_WIREFRAME : D3D12_FILL_MODE_SOLID;
        }

        D3D12_CULL_MODE ToCullMode(RHICullMode cull_mode)
        {
            switch (cull_mode)
            {
            case RHICullMode::None:
                return D3D12_CULL_MODE_NONE;
            case RHICullMode::Front:
                return D3D12_CULL_MODE_FRONT;
            default:
                return D3D12_CULL_MODE_BACK;
            }
        }

        D3D12_COMPARISON_FUNC ToCompareFunc(RHICompareOp compare_op)
        {
            switch (compare_op)
            {
            case RHICompareOp::Never: return D3D12_COMPARISON_FUNC_NEVER;
            case RHICompareOp::Less: return D3D12_COMPARISON_FUNC_LESS;
            case RHICompareOp::Equal: return D3D12_COMPARISON_FUNC_EQUAL;
            case RHICompareOp::LessEqual: return D3D12_COMPARISON_FUNC_LESS_EQUAL;
            case RHICompareOp::Greater: return D3D12_COMPARISON_FUNC_GREATER;
            case RHICompareOp::NotEqual: return D3D12_COMPARISON_FUNC_NOT_EQUAL;
            case RHICompareOp::GreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
            default: return D3D12_COMPARISON_FUNC_ALWAYS;
            }
        }

        D3D12_FILTER_TYPE ToSamplerFilterType(RHIFilter filter)
        {
            return filter == RHIFilter::Nearest ? D3D12_FILTER_TYPE_POINT : D3D12_FILTER_TYPE_LINEAR;
        }

        D3D12_FILTER ToSamplerFilter(const RHISamplerDesc& desc)
        {
            return D3D12_ENCODE_BASIC_FILTER(
                ToSamplerFilterType(desc.min_filter),
                ToSamplerFilterType(desc.mag_filter),
                ToSamplerFilterType(desc.mip_filter),
                D3D12_FILTER_REDUCTION_TYPE_STANDARD);
        }

        D3D12_TEXTURE_ADDRESS_MODE ToSamplerAddressMode(RHIAddressMode address_mode)
        {
            switch (address_mode)
            {
            case RHIAddressMode::Clamp:
                return D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            case RHIAddressMode::Mirror:
                return D3D12_TEXTURE_ADDRESS_MODE_MIRROR;
            case RHIAddressMode::Wrap:
            default:
                return D3D12_TEXTURE_ADDRESS_MODE_WRAP;
            }
        }

        bool CreateRootSignatureFromShaderBytecode(ID3D12Device* device, const RHIShader& shader,
            const char* pipeline_name, ComPtr<ID3D12RootSignature>& root_signature_out, RHIPipelineDX12::RootSignatureBindingTable& binding_table_out)
        {
            if (!device || !shader.GetBytecode() || shader.GetBytecodeSize() == 0)
            {
                backlog::Post(String(pipeline_name) + " has invalid shader bytecode for root signature", backlog::LogLevel::Error);
                return false;
            }

            ComPtr<ID3DBlob> serialized_root_signature;
            if (FAILED(D3DGetBlobPart(shader.GetBytecode(), static_cast<SIZE_T>(shader.GetBytecodeSize()),
                    D3D_BLOB_ROOT_SIGNATURE, 0, &serialized_root_signature)))
            {
                backlog::Post(String(pipeline_name) + " shader does not contain a default root signature", backlog::LogLevel::Error);
                return false;
            }

            ComPtr<ID3D12VersionedRootSignatureDeserializer> root_signature_deserializer;
            if (FAILED(D3D12CreateVersionedRootSignatureDeserializer(
                    serialized_root_signature->GetBufferPointer(),
                    serialized_root_signature->GetBufferSize(),
                    IID_PPV_ARGS(&root_signature_deserializer))))
            {
                backlog::Post(String(pipeline_name) + " shader root signature deserialization failed", backlog::LogLevel::Error);
                return false;
            }

            if (FAILED(device->CreateRootSignature(0, serialized_root_signature->GetBufferPointer(),
                    serialized_root_signature->GetBufferSize(), IID_PPV_ARGS(&root_signature_out))))
            {
                backlog::Post(String(pipeline_name) + " root signature creation failed", backlog::LogLevel::Error);
                return false;
            }

            const D3D12_VERSIONED_ROOT_SIGNATURE_DESC* desc = nullptr;
            root_signature_deserializer->GetRootSignatureDescAtVersion(D3D_ROOT_SIGNATURE_VERSION_1_1, &desc);
            binding_table_out.Build(*desc);

            return true;
        }
    }

    RHIDeviceDX12::RHIDeviceDX12(const RHIDeviceDesc& desc)
        : device_desc(desc)
    {

        UINT factory_flags = 0;
#ifdef _DEBUG
        if (desc.enable_debug_layer)
        {
            factory_flags |= DXGI_CREATE_FACTORY_DEBUG;

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
            else
            {
                factory_flags &= ~DXGI_CREATE_FACTORY_DEBUG;
            }
            
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
            else
            {
                factory_flags &= ~DXGI_CREATE_FACTORY_DEBUG;
            }
        }
#endif
        
        HRESULT factory_result = CreateDXGIFactory2(factory_flags, IID_PPV_ARGS(&factory));
        if (FAILED(factory_result))
        {
            // fallback
            factory_result = CreateDXGIFactory2(0u, IID_PPV_ARGS(&factory));
            if (FAILED(factory_result))
            {
                return;
            }
        }

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

        D3D12_FEATURE_DATA_D3D12_OPTIONS options = {};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS, &options, sizeof(options))))
        {
            if (options.ResourceHeapTier >= D3D12_RESOURCE_HEAP_TIER_2)
            {
                AddDeviceFeature(feature_flags, RHIDeviceFeature::MixedResourceHeap);
            }

            if (options.ResourceBindingTier >= D3D12_RESOURCE_BINDING_TIER_3)
            {
                AddDeviceFeature(feature_flags, RHIDeviceFeature::Bindless);
            }
        }

        D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options5, sizeof(options5))))
        {
            if (options5.RaytracingTier != D3D12_RAYTRACING_TIER_NOT_SUPPORTED)
            {
                AddDeviceFeature(feature_flags, RHIDeviceFeature::RayTracing);
            }
        }

        D3D12_FEATURE_DATA_ARCHITECTURE1 architecture = {};
        architecture.NodeIndex = 0;
        if (SUCCEEDED(device->CheckFeatureSupport(D3D12_FEATURE_ARCHITECTURE1, &architecture, sizeof(architecture))))
        {
            if (architecture.UMA)
            {
                AddDeviceFeature(feature_flags, RHIDeviceFeature::UMA);
            }
        }
        
        DXGI_ADAPTER_DESC1 selected_adapter_desc = {};
        if (adapter)
        {
            adapter->GetDesc1(&selected_adapter_desc);
        }

        const String adapter_name = won::utils::EncodeUtf8(selected_adapter_desc.Description);
        if (!adapter_name.empty())
        {
            wonlog("DX12 Adapter: %s", adapter_name.c_str());
        }
        wonlog("DX12 Feature Level: %s", FeatureLevelToString(features.MaxSupportedFeatureLevel()));
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

        descriptor_allocator = std::make_shared<DescriptorAllocatorDX12>(device);
        if (!descriptor_allocator->IsValid())
        {
            backlog::Post("Failed to create descriptor allocator", backlog::LogLevel::Error);
            descriptor_allocator.reset();
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
        if (graphics_context)
        {
            graphics_context->WaitIdle();
        }
        if (compute_context)
        {
            compute_context->WaitIdle();
        }
        if (copy_context)
        {
            copy_context->WaitIdle();
        }

        graphics_context.reset();
        compute_context.reset();
        copy_context.reset();
        descriptor_allocator.reset();
        resource_allocator.Reset();
        device.Reset();
        adapter.Reset();
        factory.Reset();
    }

    void RHIDeviceDX12::BeginFrame(uint32 frame_index)
    {
        if (descriptor_allocator)
        {
            descriptor_allocator->BeginFrame(frame_index);
        }
    }

    uint32 RHIDeviceDX12::GetFeatureFlags() const
    {
        return feature_flags;
    }

    bool RHIDeviceDX12::HasFeature(RHIDeviceFeature feature) const
    {
        return (feature_flags & static_cast<uint32>(feature)) != 0;
    }

    std::shared_ptr<RHIFence> RHIDeviceDX12::CreateFence(uint64 initial_value)
    {
        if (!device)
        {
            return nullptr;
        }

        return std::make_shared<RHIFenceDX12>(device, initial_value);
    }

    std::shared_ptr<RHICommandAllocator> RHIDeviceDX12::CreateCommandAllocator(RHIQueueType type)
    {
        return std::make_shared<RHICommandAllocatorDX12>(type, device);
    }

    std::shared_ptr<RHICommandList> RHIDeviceDX12::CreateCommandList(RHIQueueType type)
    {
        return std::make_shared<RHICommandListDX12>(type, device, descriptor_allocator);
    }

    std::shared_ptr<RHIQueryHeap> RHIDeviceDX12::CreateQueryHeap(const RHIQueryHeapDesc& desc)
    {
        if (!device || desc.query_count == 0)
        {
            return nullptr;
        }

        D3D12_QUERY_HEAP_DESC query_heap_desc = {};
        query_heap_desc.Count = desc.query_count;
        query_heap_desc.NodeMask = 0;
        switch (desc.type)
        {
        case RHIQueryType::Occlusion:
            query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_OCCLUSION;
            break;
        case RHIQueryType::Timestamp:
        default:
            query_heap_desc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
            break;
        }

        ComPtr<ID3D12QueryHeap> query_heap;
        if (FAILED(device->CreateQueryHeap(&query_heap_desc, IID_PPV_ARGS(&query_heap))) || !query_heap)
        {
            backlog::Post("Failed to create query heap", backlog::LogLevel::Error);
            return nullptr;
        }

        return std::make_shared<RHIQueryHeapDX12>(desc, std::move(query_heap));
    }

    std::shared_ptr<RHIResource> RHIDeviceDX12::CreateBuffer(const RHIBufferDesc& desc,
        const void* initial_data, Size initial_size)
    {
        if (!resource_allocator || !device || desc.size == 0)
        {
            return nullptr;
        }

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if (HasBindFlag(desc.bind_flags, RHIBindFlags::UnorderedAccess))
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        Size buffer_size = desc.size;
        if (HasBindFlag(desc.bind_flags, RHIBindFlags::ConstantBuffer))
        {
            buffer_size = AlignConstantBufferByteSize(buffer_size);
        }

        D3D12_HEAP_TYPE heap_type = D3D12_HEAP_TYPE_DEFAULT;
        if (desc.usage == RHIResourceUsage::Upload)
        {
            heap_type = D3D12_HEAP_TYPE_UPLOAD;
        }
        else if (desc.usage == RHIResourceUsage::Readback)
        {
            heap_type = D3D12_HEAP_TYPE_READBACK;
        }

        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
        if (heap_type == D3D12_HEAP_TYPE_UPLOAD)
        {
            initial_state = D3D12_RESOURCE_STATE_GENERIC_READ;
        }
        else if (heap_type == D3D12_HEAP_TYPE_READBACK)
        {
            initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
        }
        //else if (desc.usage == RHIResourceUsage::Default && initial_data && initial_size > 0)
        //{
        //    initial_state = D3D12_RESOURCE_STATE_COPY_DEST;
        //}

        D3D12MA::ALLOCATION_DESC allocation_desc = {};
        allocation_desc.HeapType = heap_type;

        D3D12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(buffer_size), flags);

        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        if (FAILED(resource_allocator->CreateResource(&allocation_desc, &resource_desc, initial_state,
                nullptr, &allocation, IID_PPV_ARGS(resource.GetAddressOf()))))
        {
            backlog::Post("Failed to create buffer resource", backlog::LogLevel::Error);
            return nullptr;
        }

        RHIResourceDesc resource_info = {};
        resource_info.type = RHIResourceType::Buffer;
        resource_info.buffer_desc = desc;
        resource_info.buffer_desc.size = buffer_size;
        auto buffer_resource = std::make_shared<RHIResourceDX12>(resource_info, std::move(resource), allocation, descriptor_allocator);
        buffer_resource->SetCurrentState(initial_state);

        if (initial_data && initial_size > 0)
        {
            if (desc.usage == RHIResourceUsage::Upload)
            {
                void* mapped_data = buffer_resource->GetMappedData();
                if (mapped_data)
                {
                    const Size copy_size = initial_size < buffer_size ? initial_size : buffer_size;
                    std::memcpy(mapped_data, initial_data, copy_size);
                }
                else
                {
                    backlog::Post("Failed to access persistent mapped upload buffer", backlog::LogLevel::Error);
                }
            }
            else if (desc.usage == RHIResourceUsage::Default)
            {
                D3D12MA::ALLOCATION_DESC upload_allocation_desc = {};
                upload_allocation_desc.HeapType = D3D12_HEAP_TYPE_UPLOAD;

                D3D12_RESOURCE_DESC upload_resource_desc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(buffer_size), D3D12_RESOURCE_FLAG_NONE);

                ComPtr<ID3D12Resource> upload_native_resource;
                D3D12MA::Allocation* upload_allocation = nullptr;
                if (FAILED(resource_allocator->CreateResource(&upload_allocation_desc, &upload_resource_desc,
                        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, &upload_allocation, IID_PPV_ARGS(upload_native_resource.GetAddressOf()))))
                {
                    backlog::Post("Failed to create upload staging buffer", backlog::LogLevel::Error);
                    return nullptr;
                }

                RHIBufferDesc upload_buffer_desc = {};
                upload_buffer_desc.size = buffer_size;
                upload_buffer_desc.usage = RHIResourceUsage::Upload;
                upload_buffer_desc.bind_flags = RHIBindFlags::None;
                RHIResourceDesc upload_resource_info = {};
                upload_resource_info.type = RHIResourceType::Buffer;
                upload_resource_info.buffer_desc = upload_buffer_desc;
                auto upload_resource = std::make_shared<RHIResourceDX12>(upload_resource_info,
                    std::move(upload_native_resource),
                    upload_allocation,
                    descriptor_allocator);
                upload_resource->SetCurrentState(D3D12_RESOURCE_STATE_GENERIC_READ);

                void* mapped_data = upload_resource->GetMappedData();
                if (!mapped_data)
                {
                    backlog::Post("Failed to map upload staging buffer", backlog::LogLevel::Error);
                    return nullptr;
                }

                const Size copy_size = initial_size < buffer_size ? initial_size : buffer_size;
                std::memcpy(mapped_data, initial_data, copy_size);

                std::shared_ptr<RHIContext> upload_context = GetContext(RHIQueueType::Copy);
                RHIQueueType upload_queue_type = RHIQueueType::Copy;
                if (!upload_context)
                {
                    // fallback to Graphics Queue
                    upload_context = GetContext(RHIQueueType::Graphics);
                    upload_queue_type = RHIQueueType::Graphics;
                }

                if (!upload_context)
                {
                    backlog::Post("No available context for default buffer upload", backlog::LogLevel::Error);
                    return nullptr;
                }

                auto upload_allocator = CreateCommandAllocator(upload_queue_type);
                auto upload_command_list = CreateCommandList(upload_queue_type);
                if (!upload_allocator || !upload_command_list)
                {
                    backlog::Post("Failed to create upload command objects", backlog::LogLevel::Error);
                    return nullptr;
                }

                upload_allocator->Reset();
                upload_command_list->Begin(*upload_allocator);
                upload_command_list->TransitionResource(*buffer_resource, RHIResourceState::CopyDest);
                upload_command_list->CopyResource(*buffer_resource, *upload_resource);
                upload_command_list->TransitionResource(*buffer_resource, RHIResourceState::Undefined);
                upload_command_list->End();

                auto upload_fence = CreateFence(0);
                if (upload_fence)
                {
                    const uint64 upload_fence_value = upload_context->Submit(*upload_command_list, upload_fence.get());
                    if (upload_fence_value > 0)
                    {
                        upload_fence->Wait(upload_fence_value);
                    }
                    else
                    {
                        upload_context->WaitIdle();
                    }
                }
                else
                {
                    upload_context->Submit(*upload_command_list);
                    upload_context->WaitIdle();
                }
            }
        }

        return buffer_resource;
    }

    std::shared_ptr<RHIResource> RHIDeviceDX12::CreateTexture(const RHITextureDesc& desc,
        const void* initial_data, Size initial_size)
    {
        if (!resource_allocator || !device || desc.width == 0 || desc.height == 0)
        {
            return nullptr;
        }

        D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
        if (HasBindFlag(desc.bind_flags, RHIBindFlags::RenderTarget))
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        }
        if (HasBindFlag(desc.bind_flags, RHIBindFlags::DepthStencil))
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        }
        if (HasBindFlag(desc.bind_flags, RHIBindFlags::UnorderedAccess))
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }
        if (desc.mip_levels > 1 &&
            (desc.format == RHIFormat::R8G8B8A8Unorm || desc.format == RHIFormat::R8G8B8A8UnormSrgb) &&
            !HasBindFlag(desc.bind_flags, RHIBindFlags::DepthStencil))
        {
            flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        }

        D3D12_RESOURCE_DIMENSION dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        uint16 depth_or_array_size = static_cast<uint16>(desc.array_layers);
        if (desc.depth > 1)
        {
            dimension = D3D12_RESOURCE_DIMENSION_TEXTURE3D;
            depth_or_array_size = static_cast<uint16>(desc.depth);
        }

        D3D12_RESOURCE_DESC resource_desc = {};
        resource_desc.Dimension = dimension;
        resource_desc.Alignment = 0;
        resource_desc.Width = desc.width;
        resource_desc.Height = desc.height;
        resource_desc.DepthOrArraySize = depth_or_array_size;
        resource_desc.MipLevels = static_cast<uint16>(desc.mip_levels);
        resource_desc.Format = ToDXGIResourceFormat(desc.format);
        resource_desc.SampleDesc.Count = desc.sample_count;
        resource_desc.SampleDesc.Quality = 0;
        resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
        resource_desc.Flags = flags;

        D3D12MA::ALLOCATION_DESC allocation_desc = {};
        allocation_desc.HeapType = D3D12_HEAP_TYPE_DEFAULT;

        D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
        if (HasBindFlag(desc.bind_flags, RHIBindFlags::DepthStencil))
        {
            initial_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
        }
        else if (HasBindFlag(desc.bind_flags, RHIBindFlags::RenderTarget))
        {
            initial_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
        }

        D3D12_CLEAR_VALUE optimized_clear_value = {};
        D3D12_CLEAR_VALUE* optimized_clear_value_ptr = nullptr;
        if (HasBindFlag(desc.bind_flags, RHIBindFlags::DepthStencil))
        {
            optimized_clear_value.Format = ToDXGIDsvFormat(desc.format);
            optimized_clear_value.DepthStencil.Depth = OPTIMIZED_FAST_CLEAR_DEPTH;
            optimized_clear_value.DepthStencil.Stencil = OPTIMIZED_FAST_CLEAR_STENCIL;
            optimized_clear_value_ptr = &optimized_clear_value;
        }
        else if (HasBindFlag(desc.bind_flags, RHIBindFlags::RenderTarget))
        {
            optimized_clear_value.Format = ToDXGIResourceFormat(desc.format);
            optimized_clear_value.Color[0] = OPTIMIZED_FAST_CLEAR_COLOR[0];
            optimized_clear_value.Color[1] = OPTIMIZED_FAST_CLEAR_COLOR[1];
            optimized_clear_value.Color[2] = OPTIMIZED_FAST_CLEAR_COLOR[2];
            optimized_clear_value.Color[3] = OPTIMIZED_FAST_CLEAR_COLOR[3];
            optimized_clear_value_ptr = &optimized_clear_value;
        }

        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        if (FAILED(resource_allocator->CreateResource(&allocation_desc, &resource_desc, initial_state,
                optimized_clear_value_ptr, &allocation, IID_PPV_ARGS(resource.GetAddressOf()))))
        {
            backlog::Post("Failed to create texture resource", backlog::LogLevel::Error);
            return nullptr;
        }

        RHIResourceDesc resource_info = {};
        resource_info.type = (dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? RHIResourceType::Texture3D : RHIResourceType::Texture2D;
        resource_info.texture_desc = desc;
        auto texture_resource = std::make_shared<RHIResourceDX12>(resource_info, std::move(resource), allocation, descriptor_allocator);
        texture_resource->SetCurrentState(initial_state);

        if (initial_data && initial_size > 0)
        {
            uint32 bytes_per_pixel = 0u;
            uint32 block_size = 0u;
            switch (desc.format)
            {
            case RHIFormat::BC1Unorm:
            case RHIFormat::BC1UnormSrgb:
            case RHIFormat::BC4Unorm:
            case RHIFormat::BC4Snorm:
                block_size = 8u;
                break;
            case RHIFormat::BC2Unorm:
            case RHIFormat::BC2UnormSrgb:
            case RHIFormat::BC3Unorm:
            case RHIFormat::BC3UnormSrgb:
            case RHIFormat::BC5Unorm:
            case RHIFormat::BC5Snorm:
            case RHIFormat::BC6HUf16:
            case RHIFormat::BC6HSf16:
            case RHIFormat::BC7Unorm:
            case RHIFormat::BC7UnormSrgb:
                block_size = 16u;
                break;
            default:
                GetFormatBytesPerPixel(desc.format, bytes_per_pixel);
                break;
            }

            const bool compressed_texture = block_size > 0u;
            const uint32 upload_mip_count = compressed_texture ? desc.mip_levels : 1u;
            UINT64 total_size = 0;
            std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> footprints;
            std::vector<uint64> row_sizes;
            std::vector<uint> num_rows;

            footprints.resize(upload_mip_count); // uncompressed uploads mip 0; compressed uploads stored mip chain
            row_sizes.resize(footprints.size());
            num_rows.resize(footprints.size());

            device->GetCopyableFootprints(&resource_desc, 0, upload_mip_count, 0, footprints.data(), num_rows.data(), row_sizes.data(), &total_size);

            RHIBufferDesc upload_buffer_desc = {};
            upload_buffer_desc.size = total_size;
            upload_buffer_desc.usage = RHIResourceUsage::Upload;
            upload_buffer_desc.bind_flags = RHIBindFlags::None;
            std::shared_ptr<RHIResource> upload_buffer = CreateBuffer(upload_buffer_desc);

            void* mapped_data = upload_buffer->GetMappedData();

            std::shared_ptr<RHIContext> upload_context = GetContext(RHIQueueType::Graphics);
            RHIQueueType upload_queue_type = RHIQueueType::Graphics;

            auto upload_allocator = CreateCommandAllocator(upload_queue_type);
            auto upload_command_list = CreateCommandList(upload_queue_type);
            auto upload_command_list_dx12 = dynamic_cast<RHICommandListDX12*>(upload_command_list.get());
            auto texture_resource_dx12 = dynamic_cast<RHIResourceDX12*>(texture_resource.get());
            auto upload_buffer_dx12 = dynamic_cast<RHIResourceDX12*>(upload_buffer.get());

            upload_allocator->Reset();
            upload_command_list->Begin(*upload_allocator);
            upload_command_list->TransitionResource(*texture_resource, RHIResourceState::CopyDest);

            Size source_offset = 0;
            for (size_t i = 0; i < footprints.size(); ++i)
            {
                const uint32 mip_width = (std::max)(1u, desc.width >> static_cast<uint32>(i));
                const uint32 mip_height = (std::max)(1u, desc.height >> static_cast<uint32>(i));
                D3D12_SUBRESOURCE_DATA data{};
                data.pData = static_cast<const uint8*>(initial_data) + source_offset;
                if (block_size > 0u)
                {
                    const uint32 block_width = (mip_width + 3u) / 4u;
                    const uint32 block_height = (mip_height + 3u) / 4u;
                    data.RowPitch = static_cast<LONG_PTR>(block_width) * static_cast<LONG_PTR>(block_size);
                    data.SlicePitch = static_cast<LONG_PTR>(block_width) * static_cast<LONG_PTR>(block_height) * static_cast<LONG_PTR>(block_size);
                }
                else
                {
                    data.RowPitch = static_cast<LONG_PTR>(mip_width) * static_cast<LONG_PTR>(bytes_per_pixel);
                    data.SlicePitch = static_cast<LONG_PTR>(mip_width) * static_cast<LONG_PTR>(mip_height) * static_cast<LONG_PTR>(bytes_per_pixel);
                }
                if (source_offset + static_cast<Size>(data.SlicePitch) > initial_size)
                {
                    return nullptr;
                }

                D3D12_MEMCPY_DEST DestData = {};
                DestData.pData = (void*)((UINT64)mapped_data + footprints[i].Offset);
                DestData.RowPitch = (SIZE_T)footprints[i].Footprint.RowPitch;
                DestData.SlicePitch = (SIZE_T)footprints[i].Footprint.RowPitch * (SIZE_T)num_rows[i];
                MemcpySubresource(&DestData, &data, (SIZE_T)row_sizes[i], num_rows[i], footprints[i].Footprint.Depth);

                CD3DX12_TEXTURE_COPY_LOCATION Dst(texture_resource_dx12->GetResource(), UINT(i));
                CD3DX12_TEXTURE_COPY_LOCATION Src(upload_buffer_dx12->GetResource(), footprints[i]);
                upload_command_list_dx12->GetCommandList()->CopyTextureRegion(
                    &Dst,
                    0,
                    0,
                    0,
                    &Src,
                    nullptr
                );
                source_offset += static_cast<Size>(data.SlicePitch);
            }

            upload_command_list->TransitionResource(*texture_resource, RHIResourceState::ShaderRead);
            
            upload_command_list->End();

            auto upload_fence = CreateFence(0);
            if (upload_fence)
            {
                const uint64 upload_fence_value = upload_context->Submit(*upload_command_list, upload_fence.get());
                if (upload_fence_value > 0)
                {
                    upload_fence->Wait(upload_fence_value);
                }
                else
                {
                    upload_context->WaitIdle();
                }
            }
            else
            {
                upload_context->Submit(*upload_command_list);
                upload_context->WaitIdle();
            }
        }

        return texture_resource;
    }

    Size RHIDeviceDX12::GetMinOffsetAlignment(const RHIBufferDesc& desc) const
    {
        Size alignment = 1;
        const uint32 bind_flags = static_cast<uint32>(desc.bind_flags);
        if ((bind_flags & static_cast<uint32>(RHIBindFlags::ConstantBuffer)) != 0)
        {
            alignment = static_cast<Size>(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT);
        }

        return alignment;
    }

    bool RHIDeviceDX12::CreateSubresource(RHIResource& resource,
        const RHISubresourceDesc& desc,
        RHISubresourceHandle* out_handle)
    {
        auto* resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return false;
        }

        if (resource_dx12->FindSubresource(desc, out_handle))
        {
            return true;
        }

        return resource_dx12->CreateSubresource(desc, out_handle);
    }

    bool RHIDeviceDX12::GetMemoryUsage(RHIMemoryUsage& out_usage)
    {
        out_usage = {};
        if (!resource_allocator)
        {
            return false;
        }

        D3D12MA::Budget local_budget = {};
        D3D12MA::Budget non_local_budget = {};
        resource_allocator->GetBudget(&local_budget, &non_local_budget);

        out_usage.local.allocation_bytes = local_budget.Stats.AllocationBytes;
        out_usage.local.block_bytes = local_budget.Stats.BlockBytes;
        out_usage.local.usage_bytes = local_budget.UsageBytes;
        out_usage.local.budget_bytes = local_budget.BudgetBytes;
        out_usage.non_local.allocation_bytes = non_local_budget.Stats.AllocationBytes;
        out_usage.non_local.block_bytes = non_local_budget.Stats.BlockBytes;
        out_usage.non_local.usage_bytes = non_local_budget.UsageBytes;
        out_usage.non_local.budget_bytes = non_local_budget.BudgetBytes;
        return true;
    }

    std::shared_ptr<RHIPipeline> RHIDeviceDX12::CreateGraphicsPipeline(
        const RHIGraphicsPipelineDesc& desc)
    {
        if (!device || !desc.vertex_shader)
        {
            backlog::Post("Graphics pipeline requires vertex shader", backlog::LogLevel::Error);
            return nullptr;
        }

        ComPtr<ID3D12RootSignature> root_signature;
        RHIPipelineDX12::RootSignatureBindingTable binding_table;
        if (!CreateRootSignatureFromShaderBytecode(device.Get(), *desc.vertex_shader,
                "Graphics pipeline", root_signature, binding_table))
        {
            return nullptr;
        }

        Vector<D3D12_INPUT_ELEMENT_DESC> input_elements;

        for (const auto& element : desc.input_layout)
        {
            D3D12_INPUT_ELEMENT_DESC input_element = {};
            input_element.SemanticName = element.semantic_name.c_str();
            input_element.SemanticIndex = element.semantic_index;
            input_element.Format = ToDXGIFormat(element.format);
            input_element.InputSlot = element.input_slot;
            input_element.AlignedByteOffset = element.byte_offset;
            input_element.InputSlotClass = element.per_instance
                ? D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA
                : D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA;
            input_element.InstanceDataStepRate = element.instance_step_rate;
            input_elements.push_back(input_element);
        }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature = root_signature.Get();
        pso_desc.VS = CD3DX12_SHADER_BYTECODE(desc.vertex_shader->GetBytecode(), desc.vertex_shader->GetBytecodeSize());
        if (desc.pixel_shader)
        {
            pso_desc.PS = CD3DX12_SHADER_BYTECODE(desc.pixel_shader->GetBytecode(), desc.pixel_shader->GetBytecodeSize());
        }
        
        pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (desc.blend.enable)
        {
            pso_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            // Alpha: src_alpha / inv_src_alpha. Additive: src_alpha / one. Premultiplied: one / inv_src_alpha.
            pso_desc.BlendState.RenderTarget[0].SrcBlend = desc.blend.mode == RHIBlendMode::Premultiplied ? D3D12_BLEND_ONE : D3D12_BLEND_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].DestBlend = desc.blend.mode == RHIBlendMode::Additive ? D3D12_BLEND_ONE : D3D12_BLEND_INV_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_INV_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
        }
        pso_desc.SampleMask = UINT_MAX;
        pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
        pso_desc.RasterizerState.FillMode = ToFillMode(desc.raster.fill_mode);
        pso_desc.RasterizerState.CullMode = ToCullMode(desc.raster.cull_mode);
        pso_desc.RasterizerState.FrontCounterClockwise = desc.raster.front_ccw ? TRUE : FALSE;
        pso_desc.RasterizerState.DepthClipEnable = desc.raster.depth_clip_enable ? TRUE : FALSE;
        pso_desc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
        const DXGI_FORMAT dsv_format = ToDXGIFormat(desc.depth_stencil_format);
        const bool has_depth_stencil = dsv_format != DXGI_FORMAT_UNKNOWN;
        pso_desc.DepthStencilState.DepthEnable = (has_depth_stencil && desc.depth_stencil.depth_test) ? TRUE : FALSE;
        pso_desc.DepthStencilState.DepthWriteMask = (has_depth_stencil && desc.depth_stencil.depth_write) ? D3D12_DEPTH_WRITE_MASK_ALL : D3D12_DEPTH_WRITE_MASK_ZERO;
        pso_desc.DepthStencilState.DepthFunc = ToCompareFunc(desc.depth_stencil.depth_compare);
        pso_desc.InputLayout = { input_elements.data(), static_cast<UINT>(input_elements.size()) };
        pso_desc.PrimitiveTopologyType = ToTopologyType(desc.topology);
        const uint32 max_render_target_count = D3D12_SIMULTANEOUS_RENDER_TARGET_COUNT;
        const uint32 render_target_count = static_cast<uint32>(desc.render_target_formats.size());
        pso_desc.NumRenderTargets = (render_target_count < max_render_target_count) ? render_target_count : max_render_target_count;
        for (uint32 i = 0; i < pso_desc.NumRenderTargets; ++i)
        {
            pso_desc.RTVFormats[i] = ToDXGIFormat(desc.render_target_formats[i]);
        }
        pso_desc.DSVFormat = dsv_format;
        pso_desc.SampleDesc.Count = desc.sample_count > 0 ? desc.sample_count : 1;

        ComPtr<ID3D12PipelineState> pipeline_state;
        if (FAILED(device->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state))))
        {
            backlog::Post("Failed to create graphics pipeline state", backlog::LogLevel::Error);
            return nullptr;
        }

        return std::make_shared<RHIPipelineDX12>(false, std::move(pipeline_state), std::move(root_signature), std::move(binding_table));
    }

    std::shared_ptr<RHIPipeline> RHIDeviceDX12::CreateComputePipeline(
        const RHIComputePipelineDesc& desc)
    {
        if (!device || !desc.compute_shader)
        {
            backlog::Post("Compute pipeline requires compute shader", backlog::LogLevel::Error);
            return nullptr;
        }

        ComPtr<ID3D12RootSignature> root_signature;
        RHIPipelineDX12::RootSignatureBindingTable binding_table;
        if (!CreateRootSignatureFromShaderBytecode(device.Get(), *desc.compute_shader,
                "Compute pipeline", root_signature, binding_table))
        {
            return nullptr;
        }

        D3D12_COMPUTE_PIPELINE_STATE_DESC pso_desc = {};
        pso_desc.pRootSignature = root_signature.Get();
        pso_desc.CS = CD3DX12_SHADER_BYTECODE(desc.compute_shader->GetBytecode(), desc.compute_shader->GetBytecodeSize());

        ComPtr<ID3D12PipelineState> pipeline_state;
        if (FAILED(device->CreateComputePipelineState(&pso_desc, IID_PPV_ARGS(&pipeline_state))))
        {
            backlog::Post("Failed to create compute pipeline state", backlog::LogLevel::Error);
            return nullptr;
        }

        return std::make_shared<RHIPipelineDX12>(true, std::move(pipeline_state), std::move(root_signature), std::move(binding_table));
    }

    std::shared_ptr<RHISampler> RHIDeviceDX12::CreateSampler(const RHISamplerDesc& desc)
    {
        if (!device || !descriptor_allocator || !descriptor_allocator->IsValid())
        {
            backlog::Post("Failed to create sampler because DX12 device is not initialized", backlog::LogLevel::Error);
            return nullptr;
        }

        D3D12_SAMPLER_DESC sampler_desc = {};
        sampler_desc.Filter = ToSamplerFilter(desc);
        sampler_desc.AddressU = ToSamplerAddressMode(desc.address_u);
        sampler_desc.AddressV = ToSamplerAddressMode(desc.address_v);
        sampler_desc.AddressW = ToSamplerAddressMode(desc.address_w);
        sampler_desc.MipLODBias = desc.mip_lod_bias;
        sampler_desc.MaxAnisotropy = 1;
        sampler_desc.ComparisonFunc = D3D12_COMPARISON_FUNC_NONE;
        sampler_desc.BorderColor[0] = 0.0f;
        sampler_desc.BorderColor[1] = 0.0f;
        sampler_desc.BorderColor[2] = 0.0f;
        sampler_desc.BorderColor[3] = 0.0f;
        sampler_desc.MinLOD = desc.min_lod;
        sampler_desc.MaxLOD = desc.max_lod;

        int descriptor_index = -1;
        if (!descriptor_allocator->CreateSamplerDescriptor(sampler_desc, descriptor_index))
        {
            backlog::Post("Failed to allocate sampler descriptor", backlog::LogLevel::Error);
            return nullptr;
        }

        return std::make_shared<RHISamplerDX12>(desc, descriptor_index, descriptor_allocator);
    }

    std::shared_ptr<RHIContext> RHIDeviceDX12::GetContext(RHIQueueType type)
    {
        switch (type)
        {
        case RHIQueueType::Graphics:
            return graphics_context;
        case RHIQueueType::Compute:
            return compute_context;
        case RHIQueueType::Copy:
            return copy_context;
        default:
            return nullptr;
        }
    }

    std::shared_ptr<RHISwapchain> RHIDeviceDX12::CreateSwapchain(platform::Window& window)
    {
        if (!device || !factory || !graphics_context)
        {
            backlog::Post("Failed to create RHISwapchainDX12", backlog::LogLevel::Error);
            return nullptr;
        }

        return std::make_shared<RHISwapchainDX12>(device, factory, graphics_context, descriptor_allocator, window);
    }
}
