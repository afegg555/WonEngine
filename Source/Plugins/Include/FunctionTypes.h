#pragma once
#include "ReflectionTypes.h"

namespace won::function
{
    struct Value
    {
        won::ValueType type = won::ValueType::Unknown;
        union
        {
            const char* string_value;
            bool bool_value;
            int32_t int32_value;
            uint32_t uint32_value;
            int64_t int64_value;
            uint64_t uint64_value;
            float float_value;
            double double_value;
            float float_values[4];
            void* pointer_value;
        };
    };

    struct ParamDesc
    {
        const char* name;
        won::ValueType type;
    };

    struct Call
    {
        const Value* inputs;
        uint32_t input_count;
        Value* outputs;
        uint32_t output_capacity;
        uint32_t* output_count;
    };
}
