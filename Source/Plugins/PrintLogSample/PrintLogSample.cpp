#include "CustomFunctionExtension.h"

#include <string>

namespace won::plugin
{
    namespace
    {
        constexpr const char* plugin_id = "PrintLogSample";
        constexpr const char* plugin_version = "1.0.0";
        constexpr const char* print_function_id = "print_log_sample.print";
        constexpr const char* get_last_input_function_id = "print_log_sample.get_last_input";

        struct PrintLogSampleState
        {
            std::string last_input;
            const WonPluginHostAPI* host_api = nullptr;
        };

        bool WON_PLUGIN_CALL PrintSample(void* self, const function::Call* call)
        {
            auto state = static_cast<PrintLogSampleState*>(self);
            if (!state || !call || call->input_count != 1 || !call->inputs || call->inputs[0].type != won::ValueType::String)
            {
                return false;
            }

            state->last_input = call->inputs[0].string_value ? call->inputs[0].string_value : "";
            if (state->host_api && state->host_api->Log)
            {
                state->host_api->Log(state->last_input.c_str());
            }

            return true;
        }

        bool WON_PLUGIN_CALL GetLastInput(void* self, const function::Call* call)
        {
            auto state = static_cast<PrintLogSampleState*>(self);
            if (!state || !call || !call->outputs || call->output_capacity == 0 || !call->output_count)
            {
                return false;
            }

            call->outputs[0].type = won::ValueType::String;
            call->outputs[0].string_value = state->last_input.c_str();
            *call->output_count = 1;
            return true;
        }

        const function::ParamDesc s_print_inputs[] = {
            { "text", won::ValueType::String },
        };

        const function::ParamDesc s_get_last_input_outputs[] = {
            { "value", won::ValueType::String },
        };

        const function::Desc s_print_function_desc{
            sizeof(function::Desc),
            "Print Log",
            s_print_inputs,
            1,
            nullptr,
            0,
            &PrintSample
        };

        const function::Desc s_get_last_input_function_desc{
            sizeof(function::Desc),
            "Get Last Input",
            nullptr,
            0,
            s_get_last_input_outputs,
            1,
            &GetLastInput
        };

        const WonExtensionDesc s_extensions[] = {
            {
                sizeof(WonExtensionDesc),
                function::ExtensionType,
                print_function_id,
                &s_print_function_desc
            },
            {
                sizeof(WonExtensionDesc),
                function::ExtensionType,
                get_last_input_function_id,
                &s_get_last_input_function_desc
            }
        };
    }
}

WON_PLUGIN_EXPORT bool WON_PLUGIN_CALL WonPluginCreate(const WonPluginHostAPI* host_api, void** out_plugin, WonPluginAPI* out_api)
{
    if (!host_api || !out_plugin || !out_api || host_api->abi_version != WON_PLUGIN_ABI_VERSION)
    {
        return false;
    }

    auto state = new won::plugin::PrintLogSampleState();
    state->host_api = host_api;
    *out_plugin = state;
    out_api->abi_version = WON_PLUGIN_ABI_VERSION;
    out_api->plugin_id = won::plugin::plugin_id;
    out_api->plugin_version = won::plugin::plugin_version;
    out_api->extensions = won::plugin::s_extensions;
    out_api->extension_count = 2;
    return true;
}

WON_PLUGIN_EXPORT void WON_PLUGIN_CALL WonPluginDestroy(void* plugin)
{
    delete static_cast<won::plugin::PrintLogSampleState*>(plugin);
}
