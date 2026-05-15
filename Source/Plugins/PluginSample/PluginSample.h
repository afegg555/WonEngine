#pragma once
#include "Plugin.h"

inline constexpr const char* WON_IID_PLUGIN_SAMPLE = "PluginSample";
inline constexpr const char* WON_VID_PLUGIN_SAMPLE = "1.0.0";

namespace won::plugin
{
    // public APIs
    struct PluginSampleAPI
    {
        bool (*PrintSample)(Plugin* self, const char* input);
    };
}