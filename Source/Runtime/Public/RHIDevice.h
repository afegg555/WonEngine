#pragma once
#include "RHICommandAllocator.h"
#include "RHIContext.h"
#include "RHIPipeline.h"
#include "RHIQueryHeap.h"
#include "RHIResource.h"
#include "RHISampler.h"
#include "RHISwapchain.h"
#include "Window.h"
#include "RuntimeExport.h"

#include <memory>

namespace won::rendering
{
    enum class RHIDeviceFeature : uint32
    {
        None = 0,
        RayTracing = 1u << 0,
        MixedResourceHeap = 1u << 1,
        UMA = 1u << 2,
        Bindless = 1u << 3
    };

    inline RHIDeviceFeature operator|(RHIDeviceFeature lhs, RHIDeviceFeature rhs)
    {
        return static_cast<RHIDeviceFeature>(static_cast<uint32>(lhs) | static_cast<uint32>(rhs));
    }

    inline RHIDeviceFeature operator&(RHIDeviceFeature lhs, RHIDeviceFeature rhs)
    {
        return static_cast<RHIDeviceFeature>(static_cast<uint32>(lhs) & static_cast<uint32>(rhs));
    }

    enum class RHIBackend
    {
        Unknown,
        DirectX12,
        Vulkan,
        Metal
    };

    enum class RHIDevicePreference
    {
        Default,
        Discrete,
        Integrated,
        Software
    };

    struct RHIDeviceDesc
    {
        RHIBackend backend = RHIBackend::DirectX12;
        RHIDevicePreference preference = RHIDevicePreference::Default;
        bool enable_debug_layer = false;
        bool enable_gpu_validation = false;
    };

    struct RHIMemorySegmentUsage
    {
        uint64 allocation_bytes = 0;
        uint64 block_bytes = 0;
        uint64 usage_bytes = 0;
        uint64 budget_bytes = 0;
    };

    struct RHIMemoryUsage
    {
        RHIMemorySegmentUsage local;
        RHIMemorySegmentUsage non_local;
    };

    class WONENGINE_API RHIDevice
    {
    public:
        virtual ~RHIDevice() = default;

        virtual void BeginFrame(uint32 frame_index) = 0;
        virtual uint32 GetFeatureFlags() const = 0;
        virtual bool HasFeature(RHIDeviceFeature feature) const = 0;

        virtual std::unique_ptr<RHIFence> CreateFence(uint64 initial_value = 0) = 0;
        virtual std::unique_ptr<RHICommandAllocator> CreateCommandAllocator(RHIQueueType type) = 0;
        virtual std::unique_ptr<RHICommandList> CreateCommandList(RHIQueueType type) = 0;
        virtual std::unique_ptr<RHIQueryHeap> CreateQueryHeap(const RHIQueryHeapDesc& desc) = 0;

        virtual std::unique_ptr<RHIResource> CreateBuffer(const RHIBufferDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) = 0;

        virtual std::unique_ptr<RHIResource> CreateTexture(const RHITextureDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) = 0;

        virtual Size GetMinOffsetAlignment(const RHIBufferDesc& desc) const = 0;

        virtual std::unique_ptr<RHIMemoryBlock> AllocateMemory(Size size, Size alignment, RHIMemoryCategory category) = 0;
        virtual std::unique_ptr<RHIResource> CreatePlacedBuffer(RHIMemoryBlock& block, Size offset, const RHIBufferDesc& desc) = 0;
        virtual std::unique_ptr<RHIResource> CreatePlacedTexture(RHIMemoryBlock& block, Size offset, const RHITextureDesc& desc) = 0;
        virtual bool ReplaceResource(RHIResource& resource, RHIMemoryBlock& block, Size offset, std::unique_ptr<RHIResource>& out_retired) = 0;
        virtual Size GetResourceAllocationSize(const RHIResourceDesc& desc, Size& out_alignment) const = 0;

        virtual bool CreateSubresource(RHIResource& resource,
            const RHISubresourceDesc& desc,
            RHISubresourceHandle* out_handle) = 0;

        virtual bool ReserveSubresource(const RHISubresourceDesc& desc,
            RHISubresourceHandle* out_handle) = 0;
        virtual bool UpdateSubresource(RHIResource& resource,
            const RHISubresourceDesc& desc,
            RHISubresourceHandle handle) = 0;
        virtual void ReleaseSubresource(const RHISubresourceDesc& desc,
            RHISubresourceHandle handle) = 0;

        virtual std::unique_ptr<RHIPipeline> CreateGraphicsPipeline(
            const RHIGraphicsPipelineDesc& desc) = 0;

        virtual std::unique_ptr<RHIPipeline> CreateComputePipeline(
            const RHIComputePipelineDesc& desc) = 0;

        virtual std::unique_ptr<RHISampler> CreateSampler(const RHISamplerDesc& desc) = 0;
        virtual RHIContext* GetContext(RHIQueueType type) = 0;
        virtual std::unique_ptr<RHISwapchain> CreateSwapchain(platform::Window& window) = 0;
        virtual bool GetMemoryUsage(RHIMemoryUsage& out_usage) = 0;
    };

    WONENGINE_API std::unique_ptr<RHIDevice> CreateRHIDevice(const RHIDeviceDesc& desc);
}
