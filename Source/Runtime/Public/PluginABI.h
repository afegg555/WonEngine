#pragma once

#include <stdint.h>

#if defined(_WIN32)
#define WON_PLUGIN_CALL __cdecl
#define WON_PLUGIN_EXPORT extern "C" __declspec(dllexport)
#else
#define WON_PLUGIN_CALL
#define WON_PLUGIN_EXPORT extern "C" __attribute__((visibility("default")))
#endif

#define WON_PLUGIN_ABI_VERSION 1

enum class WonExtensionType : uint32_t
{
    Unknown = 0,
    Function,
    Component,
    System,
};

struct WonPluginHostAPI
{
    uint32_t abi_version;
    void (WON_PLUGIN_CALL* Log)(const char* message);
};

struct WonExtensionDesc
{
    uint32_t struct_size;
    WonExtensionType extension_type;
    const char* extension_id;
    const void* descriptor;
};

struct WonPluginAPI
{
    uint32_t abi_version;
    const char* plugin_id;
    const char* plugin_version;
    const WonExtensionDesc* extensions;
    uint32_t extension_count;
};

using WonPluginCreateFn = bool (WON_PLUGIN_CALL*)(const WonPluginHostAPI* host_api, void** out_plugin, WonPluginAPI* out_api);
using WonPluginDestroyFn = void (WON_PLUGIN_CALL*)(void* plugin);
