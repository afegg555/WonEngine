#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>
#include <vector>

namespace won
{
    using int8 = std::int8_t;
    using int16 = std::int16_t;
    using int32 = std::int32_t;
    using int64 = std::int64_t;

    using uint8 = std::uint8_t;
    using uint16 = std::uint16_t;
    using uint32 = std::uint32_t;
    using uint64 = std::uint64_t;

    using Size = std::size_t;
    using StringView = std::string_view;
    using String = std::string;

    template<typename T>
    using Vector = std::vector<T>;
}
