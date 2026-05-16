#pragma once
#include "PluginABI.h"

inline constexpr const char* WON_IID_PLUGIN_SAMPLE = "PluginSample";
inline constexpr const char* WON_VID_PLUGIN_SAMPLE = "1.0.0";

namespace won::plugin
{
    // public APIs
    struct PluginSampleAPI
    {
        WonPluginBool (WON_PLUGIN_CALL* PrintSample)(void* self, const char* input);
        const char* (WON_PLUGIN_CALL* GetLastInput)(void* self);
    };
}
