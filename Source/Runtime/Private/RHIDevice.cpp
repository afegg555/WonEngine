#include "RHIDevice.h"
#include "Backlog.h"

#include "RHIDeviceDX12.h"

namespace won::rendering
{
    std::shared_ptr<RHIDevice> CreateRHIDevice(const RHIDeviceDesc& desc)
    {
        std::shared_ptr<RHIDevice> device;
        switch (desc.backend)
        {
        case RHIBackend::DirectX12:
        default:
            backlog::Post("Backend : DirectX12");
            device = std::make_shared<RHIDeviceDX12>(desc);
            break;
        case RHIBackend::Vulkan:
        case RHIBackend::Metal:
        case RHIBackend::Unknown:
            backlog::Post("Backend is not supported yet, falling back to DirectX12", backlog::LogLevel::Error);
            device = std::make_shared<RHIDeviceDX12>(desc);
            break;
        }

        return device;
    }
}
