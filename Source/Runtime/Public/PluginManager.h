#pragma once
#include "PluginABI.h"
#include "RuntimeExport.h"
#include "Types.h"

namespace won::plugin
{
    class WONENGINE_API Plugin
    {
    public:
        Plugin(const String& name, void* native_handle, void* plugin_handle, const WonPluginAPI& api, WonPluginDestroyFn destroy);
        ~Plugin();

        Plugin(const Plugin&) = delete;
        Plugin& operator=(const Plugin&) = delete;

        const char* GetName() const;
        void* GetHandle() const;
        bool QueryInterface(const char* iid, const char* version_id, void** out_interface) const;
        void* QueryInterface(const char* iid, const char* version_id) const;

    private:
        String name;
        void* native_handle = nullptr;
        void* plugin_handle = nullptr;
        WonPluginAPI api = {};
        WonPluginDestroyFn destroy = nullptr;
    };

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
