#pragma once
#include "PluginABI.h"
#include "ReflectionTypes.h"

#include <stdint.h>

namespace won::plugin::function
{
    inline constexpr WonExtensionType ExtensionType = WonExtensionType::Function;

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

    using InvokeFn = bool (WON_PLUGIN_CALL*)(void* plugin, const Call* call);

    struct Desc
    {
        uint32_t struct_size;
        const char* display_name;
        const ParamDesc* inputs;
        uint32_t input_count;
        const ParamDesc* outputs;
        uint32_t output_count;
        InvokeFn Invoke;
    };
}
