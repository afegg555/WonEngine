#pragma once
#include "Types.h"

#include <functional>

#if defined(_WIN32)
#define WON_PLUGIN_CALL __cdecl
#define WON_PLUGIN_EXPORTS extern "C" __declspec(dllexport)
#else
#define WON_PLUGIN_CALL
#define WON_PLUGIN_EXPORTS extern "C" __attribute__((visibility("default")))
#endif


namespace won::plugin
{
    class IPlugin
    {
    public:
        virtual ~IPlugin() { Shutdown(); };
        virtual const char* GetName() const = 0;
        virtual const char* GetVersion() const = 0;
        virtual void* QueryInterface(const char* iid, const char* version_id) const = 0;
        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;
    };

    class PluginFactoryRegistry
    {
    public:
        static PluginFactoryRegistry& Get()
        {
            static PluginFactoryRegistry inst;
            return inst;
        }

        void Register(const std::string& name, std::function<IPlugin* ()> creator)
        {
            factories[name] = creator;
        }

        IPlugin* Create(const std::string& name)
        {
            auto it = factories.find(name);
            if (it == factories.end())
                return nullptr;
            return it->second();
        }

    private:
        std::unordered_map<std::string, std::function<IPlugin* ()>> factories;
    };

    using CreatePluginFn = IPlugin * (WON_PLUGIN_CALL*)();

#define IMPLEMENT_PLUGIN(PluginClass, PluginName)               \
    WON_PLUGIN_EXPORTS IPlugin* WON_PLUGIN_CALL CreatePlugin()    \
    {                                                           \
        return new PluginClass();                               \
    }    
}
