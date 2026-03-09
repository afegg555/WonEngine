#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::rendering
{
    class RHIObject
    {
    public:
        virtual ~RHIObject() = default;

        virtual void SetName(const String& name) = 0;
        virtual const String& GetName() const = 0;
    };
}

