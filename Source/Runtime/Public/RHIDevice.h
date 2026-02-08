#pragma once
#include "RHICommandAllocator.h"
#include "RHIContext.h"
#include "RHIPipeline.h"
#include "RHIResource.h"
#include "RHISampler.h"
#include "RuntimeExport.h"

#include <memory>

namespace won::rendering
{
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

    class WONENGINE_API RHIDevice
    {
    public:
        virtual ~RHIDevice() = default;

        virtual std::shared_ptr<RHIFence> CreateFence(uint64 initial_value = 0) = 0;
        virtual std::shared_ptr<RHICommandAllocator> CreateCommandAllocator(RHIQueueType type) = 0;
        virtual std::shared_ptr<RHICommandList> CreateCommandList(RHIQueueType type) = 0;

        virtual std::shared_ptr<RHIResource> CreateBuffer(const RHIBufferDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) = 0;

        virtual std::shared_ptr<RHIResource> CreateTexture(const RHITextureDesc& desc,
            const void* initial_data = nullptr, Size initial_size = 0) = 0;

        virtual std::shared_ptr<RHIPipeline> CreateGraphicsPipeline(
            const RHIGraphicsPipelineDesc& desc) = 0;

        virtual std::shared_ptr<RHIPipeline> CreateComputePipeline(
            const RHIComputePipelineDesc& desc) = 0;

        virtual std::shared_ptr<RHISampler> CreateSampler(const RHISamplerDesc& desc) = 0;
    };

    WONENGINE_API std::shared_ptr<RHIDevice> CreateRHIDevice(const RHIDeviceDesc& desc);
}
