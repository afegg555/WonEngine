#pragma once
#include "PluginABI.h"

#include <stdint.h>

namespace won::plugin::system
{
    inline constexpr WonExtensionType ExtensionType = WonExtensionType::System;

    enum class ExecutionPolicy : uint32_t
    {
        ParallelJob,
        Synchronous,
    };

    struct UpdateContext
    {
        void* scene;
        float delta_time;
    };

    using UpdateFn = bool (WON_PLUGIN_CALL*)(void* plugin, const UpdateContext* context);

    struct Desc
    {
        uint32_t struct_size;
        const char* display_name;
        ExecutionPolicy execution_policy;
        UpdateFn Update;
    };
}
