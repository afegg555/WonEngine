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
        bool LoadPlugin(const std::string& name);
        bool UnloadPlugin(const std::string& name);

        std::shared_ptr<IPlugin> GetPlugin(const std::string& name);

    private:
        UnorderedMap<std::string, PluginHandle> plugins;
        std::mutex vector_lock;
    };
}
