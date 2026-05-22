#pragma once

#include <stdint.h>

namespace won
{
    enum class ValueType : uint32_t
    {
        Unknown = 0,
        Bool,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float,
        Double,
        Float2,
        Float3,
        Float4,
        String,
        Pointer,
        CustomStruct, // only used by stream and field metadata, not by function::Value
    };
}
