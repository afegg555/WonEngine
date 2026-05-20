#pragma once

#include <cstdint>

namespace won
{
    enum class ValueType : std::uint32_t
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
    };
}
