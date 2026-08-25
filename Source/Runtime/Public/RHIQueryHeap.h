#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "RHIObject.h"

namespace won::rendering
{
    enum class RHIQueryType
    {
        Timestamp,
        Occlusion, // count of passed fragments
        BinaryOcclusion, // boolean
    };

    struct RHIQueryHeapDesc
    {
        RHIQueryType type = RHIQueryType::Timestamp;
        uint32 query_count = 0;
    };

    class RHIQueryHeap : public RHIObject
    {
    public:
        ~RHIQueryHeap() override = default;

        virtual const RHIQueryHeapDesc& GetDesc() const = 0;
    };
}
