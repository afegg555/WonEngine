#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIObject.h"

namespace won::rendering
{
    enum class RHIShaderStage
    {
        Vertex,
        Pixel,
        Compute,
        All
    };

    class WONENGINE_API RHIShader : public RHIObject
    {
    public:
        virtual ~RHIShader() = default;

        virtual RHIShaderStage GetStage() const = 0;
        virtual const void* GetBytecode() const = 0;
        virtual Size GetBytecodeSize() const = 0;
    };
}

