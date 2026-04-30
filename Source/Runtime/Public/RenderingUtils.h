#pragma once
#include "RHIDevice.h"
#include "RuntimeExport.h"

namespace won::resource
{
    struct Mesh;
}

namespace won::ecs
{
    class Scene;
}

namespace won::rendering::utils
{
    WONENGINE_API bool GenerateTextureMips(RHIDevice& device,
        RHIResource& texture_resource,
        const RHITextureDesc& desc);
    WONENGINE_API bool CreateRenderData(RHIDevice& device, resource::Mesh& mesh);
    WONENGINE_API bool CreateGPUBVH(RHIDevice& device, resource::Mesh& mesh);
    WONENGINE_API void BuildGPUBVH(RHIDevice* device, ecs::Scene& scene);
}
