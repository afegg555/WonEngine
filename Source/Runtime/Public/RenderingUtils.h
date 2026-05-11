#pragma once
#include "RHIDevice.h"
#include "RuntimeExport.h"
#include "Types.h"

namespace won::resource
{
    struct Image;
    struct Mesh;
}

namespace won::rendering::utils
{
    WONENGINE_API void EnqueueTextureMipGeneration(const std::shared_ptr<RHIResource>& texture_resource);
    WONENGINE_API void EnqueueGPUBVHBuild(const std::shared_ptr<resource::Mesh>& mesh);

    WONENGINE_API bool FlushEnqueuedRenderingWork(RHIDevice& device, RHICommandList& command_list, Vector<std::shared_ptr<RHIResource>>& scratch_resources);

    WONENGINE_API bool CreateRenderData(RHIDevice& device, resource::Mesh& mesh);
    WONENGINE_API bool CreateRenderData(RHIDevice& device, resource::Image& image, RHIFormat format = RHIFormat::R8G8B8A8UnormSrgb, bool generate_mips = false);
}
