#include "RHIDeviceDX12.h"
#include "Platform.h"
#include "Backlog.h"
#include "Types.h"
#include "StringUtils.h"
#include "RHIContextDX12.h"
#include "RHICommandAllocatorDX12.h"
#include "RHICommandListDX12.h"
#include "RHIResourceDX12.h"
#include "RHIPipelineDX12.h"
#include "RHISwapchainDX12.h"
#include "RHIFormatDX12.h"
#include "DescriptorAllocatorDX12.h"

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

        bool HasBindFlag(RHIBindFlags flags, RHIBindFlags flag)
        {
            return (static_cast<uint32>(flags) & static_cast<uint32>(flag)) != 0;
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

        bool CreateRootSignatureFromShaderBytecode(ID3D12Device* device, const RHIShader& shader,
            const char* pipeline_name, ComPtr<ID3D12RootSignature>& root_signature_out)
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

        const String adapter_name = utils::ToString(selected_adapter_desc.Description);
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
        device.Reset();
        adapter.Reset();
        factory.Reset();
        resource_allocator.Reset();
        descriptor_allocator.reset();
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
        (void)initial_value;
        return nullptr;
    }

    std::shared_ptr<RHICommandAllocator> RHIDeviceDX12::CreateCommandAllocator(RHIQueueType type)
    {
        return std::make_shared<RHICommandAllocatorDX12>(type, device);
    }

    std::shared_ptr<RHICommandList> RHIDeviceDX12::CreateCommandList(RHIQueueType type)
    {
        return std::make_shared<RHICommandListDX12>(type, device, descriptor_allocator);
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

        D3D12MA::ALLOCATION_DESC allocation_desc = {};
        allocation_desc.HeapType = heap_type;

        D3D12_RESOURCE_DESC resource_desc = CD3DX12_RESOURCE_DESC::Buffer(static_cast<UINT64>(desc.size), flags);

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
        auto buffer_resource = std::make_shared<RHIResourceDX12>(resource_info, std::move(resource), allocation, descriptor_allocator);

        if (initial_data && initial_size > 0)
        {
            if (desc.usage == RHIResourceUsage::Upload)
            {
                void* mapped_data = buffer_resource->GetMappedData();
                if (mapped_data)
                {
                    const Size copy_size = initial_size < desc.size ? initial_size : desc.size;
                    std::memcpy(mapped_data, initial_data, copy_size);
                }
                else
                {
                    backlog::Post("Failed to access persistent mapped upload buffer", backlog::LogLevel::Error);
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
        resource_desc.Format = ToDXGIFormat(desc.format);
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

        ComPtr<ID3D12Resource> resource;
        D3D12MA::Allocation* allocation = nullptr;
        if (FAILED(resource_allocator->CreateResource(&allocation_desc, &resource_desc, initial_state,
                nullptr, &allocation, IID_PPV_ARGS(resource.GetAddressOf()))))
        {
            backlog::Post("Failed to create texture resource", backlog::LogLevel::Error);
            return nullptr;
        }

        if (initial_data && initial_size > 0)
        {
            assert(0);
            backlog::Post("Initial data for texture upload is not implemented yet", backlog::LogLevel::Warning);
        }

        RHIResourceDesc resource_info = {};
        resource_info.type = (dimension == D3D12_RESOURCE_DIMENSION_TEXTURE3D) ? RHIResourceType::Texture3D : RHIResourceType::Texture2D;
        resource_info.texture_desc = desc;
        return std::make_shared<RHIResourceDX12>(resource_info, std::move(resource), allocation, descriptor_allocator);
    }

    bool RHIDeviceDX12::CreateSubresource(RHIResource& resource,
        const RHISubresourceDesc& desc,
        RHISubresourceHandle* out_handle)
    {
        if (!descriptor_allocator || !out_handle)
        {
            return false;
        }

        auto* resource_dx12 = dynamic_cast<RHIResourceDX12*>(&resource);
        if (!resource_dx12 || !resource_dx12->GetResource())
        {
            return false;
        }

        if (resource_dx12->FindSubresource(desc, out_handle))
        {
            return true;
        }

        D3D12_DESCRIPTOR_HEAP_TYPE heap_type = D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES;
        uint32 descriptor_index;
        if (!descriptor_allocator->CreateSubresourceDescriptor(*resource_dx12, desc, heap_type, descriptor_index))
        {
            return false;
        }

        return resource_dx12->AddSubresource(desc, heap_type, descriptor_index, out_handle);
    }

    std::shared_ptr<RHIPipeline> RHIDeviceDX12::CreateGraphicsPipeline(
        const RHIGraphicsPipelineDesc& desc)
    {
        if (!device || !desc.vertex_shader || !desc.pixel_shader)
        {
            backlog::Post("Graphics pipeline requires vertex/pixel shader", backlog::LogLevel::Error);
            return nullptr;
        }

        ComPtr<ID3D12RootSignature> root_signature;
        if (!CreateRootSignatureFromShaderBytecode(device.Get(), *desc.vertex_shader,
                "Graphics pipeline", root_signature))
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
        pso_desc.PS = CD3DX12_SHADER_BYTECODE(desc.pixel_shader->GetBytecode(), desc.pixel_shader->GetBytecodeSize());
        pso_desc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
        if (desc.blend.enable)
        {
            pso_desc.BlendState.RenderTarget[0].BlendEnable = TRUE;
            pso_desc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
            pso_desc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
            pso_desc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
            pso_desc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
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

        return std::make_shared<RHIPipelineDX12>(false, std::move(pipeline_state), std::move(root_signature));
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
        if (!CreateRootSignatureFromShaderBytecode(device.Get(), *desc.compute_shader,
                "Compute pipeline", root_signature))
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

        return std::make_shared<RHIPipelineDX12>(true, std::move(pipeline_state), std::move(root_signature));
    }

    std::shared_ptr<RHISampler> RHIDeviceDX12::CreateSampler(const RHISamplerDesc& desc)
    {
        (void)desc;
        return nullptr;
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
