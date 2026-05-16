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

// use typedef so C-compatible !!
typedef uint32_t WonPluginBool; // fixed-width bool for ABI boundaries
#define WON_PLUGIN_FALSE 0u
#define WON_PLUGIN_TRUE 1u

struct WonPluginHostAPI
{
    uint32_t abi_version;
    void (WON_PLUGIN_CALL* Log)(const char* message);
};

struct WonPluginAPI
{
    uint32_t abi_version;
    const char* iid;
    const char* version_id;
    void* api;
};

typedef WonPluginBool (WON_PLUGIN_CALL* WonPluginCreateFn)(const WonPluginHostAPI* host_api, void** out_plugin, WonPluginAPI* out_api);
typedef void (WON_PLUGIN_CALL* WonPluginDestroyFn)(void* plugin);
