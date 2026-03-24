#pragma once
#include "RHIDevice.h"
#include "RuntimeExport.h"

namespace won::rendering::utils
{
    WONENGINE_API bool GenerateTextureMips(RHIDevice& device,
        RHIResource& texture_resource,
        const RHITextureDesc& desc);
}
