#pragma once
#include "IPlugin.h"
#include "Types.h"

#include <mutex>
namespace won::plugin
{
    struct PluginHandle
    {
        std::shared_ptr<IPlugin> plugin;
        void* native_handle;
    };
    class PluginManager
    {
    public:
        bool LoadPlugin(const String& name);
        bool UnloadPlugin(const String& name);

        std::shared_ptr<IPlugin> GetPlugin(const String& name);

    private:
        UnorderedMap<String, PluginHandle> plugins;
        std::mutex vector_lock;
    };
}
