#pragma once
#include "Plugin.h"
#include "Types.h"
#include "RuntimeExport.h"

#include <mutex>
namespace won::plugin
{
    class WONENGINE_API PluginManager
    {
    public:
        PluginManager();
        ~PluginManager();

        bool LoadPlugin(const String& name);
        bool UnloadPlugin(const String& name);

        std::shared_ptr<Plugin> GetPlugin(const String& name);

    private:
        struct Impl;
        Impl* p_impl = nullptr;
    };
}
