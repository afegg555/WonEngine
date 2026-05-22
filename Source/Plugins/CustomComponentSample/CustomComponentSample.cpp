#include "CustomComponentExtension.h"
#include "CustomSystemExtension.h"

#include <cstddef>
#include <new>

namespace won::plugin
{
    namespace
    {
        struct CustomComponentSampleState
        {
            uint32_t update_count = 0;
        };

        constexpr const char* plugin_id = "CustomComponentSample";
        constexpr const char* plugin_version = "1.0.0";
        constexpr const char* special_component_id = "custom_component_sample.special_component";
        constexpr const char* special_system_id = "custom_component_sample.special_system";

        struct SpecialComponent
        {
            float speed = 1.0f;
            float amplitude = 2.0f;
            bool enabled = true;
        };

        void WON_PLUGIN_CALL ConstructSpecialComponent(void* memory)
        {
            new (memory) SpecialComponent();
        }

        void WON_PLUGIN_CALL DestructSpecialComponent(void* memory)
        {
            static_cast<SpecialComponent*>(memory)->~SpecialComponent();
        }

        void WON_PLUGIN_CALL CopySpecialComponent(void* dst, const void* src)
        {
            new (dst) SpecialComponent(*static_cast<const SpecialComponent*>(src));
        }

        const component::FieldDesc s_special_component_fields[] = {
            { "speed", won::ValueType::Float, static_cast<uint32_t>(offsetof(SpecialComponent, speed)) },
            { "amplitude", won::ValueType::Float, static_cast<uint32_t>(offsetof(SpecialComponent, amplitude)) },
            { "enabled", won::ValueType::Bool, static_cast<uint32_t>(offsetof(SpecialComponent, enabled)) },
        };

        const component::Desc s_special_component_desc{
            sizeof(component::Desc),
            "Special Component",
            sizeof(SpecialComponent),
            alignof(SpecialComponent),
            &ConstructSpecialComponent,
            &DestructSpecialComponent,
            &CopySpecialComponent,
            s_special_component_fields,
            3
        };

        bool WON_PLUGIN_CALL UpdateSpecialSystem(void* plugin, const system::UpdateContext* context)
        {
            (void)context;
            auto state = static_cast<CustomComponentSampleState*>(plugin);
            if (!state)
            {
                return false;
            }

            ++state->update_count;
            return true;
        }

        const system::Desc s_special_system_desc{
            sizeof(system::Desc),
            "Special System",
            system::ExecutionPolicy::Synchronous,
            &UpdateSpecialSystem
        };

        const WonExtensionDesc s_extensions[] = {
            {
                sizeof(WonExtensionDesc),
                component::ExtensionType,
                special_component_id,
                &s_special_component_desc
            },
            {
                sizeof(WonExtensionDesc),
                system::ExtensionType,
                special_system_id,
                &s_special_system_desc
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

    *out_plugin = new won::plugin::CustomComponentSampleState();
    out_api->abi_version = WON_PLUGIN_ABI_VERSION;
    out_api->plugin_id = won::plugin::plugin_id;
    out_api->plugin_version = won::plugin::plugin_version;
    out_api->extensions = won::plugin::s_extensions;
    out_api->extension_count = 2;
    return true;
}

WON_PLUGIN_EXPORT void WON_PLUGIN_CALL WonPluginDestroy(void* plugin)
{
    delete static_cast<won::plugin::CustomComponentSampleState*>(plugin);
}
