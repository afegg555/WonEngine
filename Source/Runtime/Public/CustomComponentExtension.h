#pragma once
#include "PluginABI.h"
#include "ValueType.h"

#include <stdint.h>

namespace won::plugin::component
{
    inline constexpr WonExtensionType ExtensionType = WonExtensionType::Component;

    struct FieldDesc
    {
        const char* name;
        won::ValueType type;
        uint32_t offset;
    };

    struct Desc
    {
        uint32_t struct_size;
        const char* display_name;
        uint32_t size;
        uint32_t alignment;
        void (WON_PLUGIN_CALL* Construct)(void* memory);
        void (WON_PLUGIN_CALL* Destruct)(void* memory);
        void (WON_PLUGIN_CALL* Copy)(void* dst, const void* src);
        const FieldDesc* fields;
        uint32_t field_count;
    };
}
