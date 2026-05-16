#include "PluginSample.h"

#include <string>

namespace won::plugin
{
    namespace
    {
        struct PluginSampleState
        {
            std::string last_input;
        };

        const WonPluginHostAPI* s_host_api = nullptr;

        WonPluginBool WON_PLUGIN_CALL PrintSample(void* self, const char* input)
        {
            auto state = static_cast<PluginSampleState*>(self);
            if (!state)
            {
                return WON_PLUGIN_FALSE;
            }

            state->last_input = input ? input : "";
            if (s_host_api && s_host_api->Log)
            {
                s_host_api->Log(state->last_input.c_str());
            }

            return WON_PLUGIN_TRUE;
        }

        const char* WON_PLUGIN_CALL GetLastInput(void* self)
        {
            auto state = static_cast<PluginSampleState*>(self);
            return state ? state->last_input.c_str() : "";
        }

        PluginSampleAPI s_api{
            &PrintSample,
            &GetLastInput
        };

    }
}

WON_PLUGIN_EXPORT WonPluginBool WON_PLUGIN_CALL WonPluginCreate(const WonPluginHostAPI* host_api, void** out_plugin, WonPluginAPI* out_api)
{
    if (!host_api || !out_plugin || !out_api || host_api->abi_version != WON_PLUGIN_ABI_VERSION)
    {
        return WON_PLUGIN_FALSE;
    }

    won::plugin::s_host_api = host_api;
    *out_plugin = new won::plugin::PluginSampleState();
    out_api->abi_version = WON_PLUGIN_ABI_VERSION;
    out_api->iid = WON_IID_PLUGIN_SAMPLE;
    out_api->version_id = WON_VID_PLUGIN_SAMPLE;
    out_api->api = &won::plugin::s_api;
    return WON_PLUGIN_TRUE;
}

WON_PLUGIN_EXPORT void WON_PLUGIN_CALL WonPluginDestroy(void* plugin)
{
    delete static_cast<won::plugin::PluginSampleState*>(plugin);
}
