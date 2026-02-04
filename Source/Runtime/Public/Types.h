#pragma once
#include "MathTypes.h"

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>
#include <vector>
#include <map>
#include <unordered_map>
#include <memory>

#define arraysize(a) (sizeof(a) / sizeof(a[0]))

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

    template <typename K, typename V>
    using Map = std::map<K, V>;

    template <typename K, typename V>
    using UnorderedMap = std::unordered_map<K, V>;
}
