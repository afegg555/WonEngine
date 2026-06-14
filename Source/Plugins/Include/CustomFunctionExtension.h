#pragma once
#include "FunctionTypes.h"
#include "PluginABI.h"

#include <stdint.h>

namespace won::plugin::function
{
    inline constexpr WonExtensionType ExtensionType = WonExtensionType::Function;

    using Value = won::function::Value;
    using ParamDesc = won::function::ParamDesc;
    using Call = won::function::Call;

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
