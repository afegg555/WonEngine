#include "PluginManager.h"
#include "Backlog.h"
#include "Platform.h"

#include <cstring>
#include <mutex>

namespace won::plugin
{
    namespace
    {
        void WON_PLUGIN_CALL HostLog(const char* message)
        {
            if (message)
            {
                backlog::Post(message);
            }
        }
    }

    Plugin::Plugin(const String& name_in, void* native_handle_in, void* plugin_handle_in, const WonPluginAPI& api_in, WonPluginDestroyFn destroy_in)
        : name(name_in)
        , native_handle(native_handle_in)
        , plugin_handle(plugin_handle_in)
        , api(api_in)
        , destroy(destroy_in)
    {
    }

    Plugin::~Plugin()
    {
        if (plugin_handle && destroy)
        {
            destroy(plugin_handle);
            plugin_handle = nullptr;
        }

#if defined(_WIN32)
        if (native_handle)
        {
            FreeLibrary((HMODULE)native_handle);
            native_handle = nullptr;
        }
#endif
    }

    const char* Plugin::GetName() const
    {
        return name.c_str();
    }

    void* Plugin::GetHandle() const
    {
        return plugin_handle;
    }

    bool Plugin::QueryInterface(const char* iid, const char* version_id, void** out_interface) const
    {
        if (!plugin_handle || !iid || !version_id || !out_interface)
        {
            return false;
        }

        *out_interface = nullptr;
        if (!api.iid || !api.version_id || !api.api)
        {
            return false;
        }
        if (std::strcmp(api.iid, iid) != 0 || std::strcmp(api.version_id, version_id) != 0)
        {
            return false;
        }

        *out_interface = api.api;
        return true;
    }

    void* Plugin::QueryInterface(const char* iid, const char* version_id) const
    {
        void* result = nullptr;
        QueryInterface(iid, version_id, &result);
        return result;
    }

    struct PluginManager::Impl
    {
        UnorderedMap<String, std::shared_ptr<Plugin>> plugins;
        std::mutex vector_lock;
    };

    PluginManager::PluginManager()
    {
        if (!p_impl)
        {
            p_impl = new Impl();
        }
    }
    PluginManager::~PluginManager()
    {
        {
            std::lock_guard<std::mutex> lock(p_impl->vector_lock);
            for (auto& entry : p_impl->plugins)
            {
                entry.second.reset();
            }
        }

        delete p_impl;
    }
    bool PluginManager::LoadPlugin(const String& name)
	{
		std::lock_guard<std::mutex> lock(p_impl->vector_lock);

		auto it = p_impl->plugins.find(name);
		if (it != p_impl->plugins.end())
		{
			won::backlog::Post("Plugin(" + name + ") Already Loaded.", won::backlog::LogLevel::Warning);
			return false;
		}

        void* handle = nullptr;
        String file_name = name;
#if defined(_DEBUG)
        file_name += "d";
#endif
#if defined(_WIN32)
        file_name += ".dll";
        handle = (void*)::LoadLibraryA(file_name.c_str());
#endif

        if (!handle)
        {
            won::backlog::Post("Failed to load plugin : " + name);
            return false;
        }

        WonPluginCreateFn create = nullptr;
        WonPluginDestroyFn destroy = nullptr;
#if defined(_WIN32)
        create = reinterpret_cast<WonPluginCreateFn>(GetProcAddress((HMODULE)handle, "WonPluginCreate"));
        destroy = reinterpret_cast<WonPluginDestroyFn>(GetProcAddress((HMODULE)handle, "WonPluginDestroy"));
#endif

        if (!create || !destroy)
        {
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            handle = nullptr;
            return false;
        }

        WonPluginHostAPI host_api = {};
        host_api.abi_version = WON_PLUGIN_ABI_VERSION;
        host_api.Log = HostLog;

        void* plugin_handle = nullptr;
        WonPluginAPI api = {};
        const WonPluginBool created = create(&host_api, &plugin_handle, &api);
        if (created == WON_PLUGIN_FALSE || !plugin_handle || api.abi_version != WON_PLUGIN_ABI_VERSION || !api.iid || !api.version_id || !api.api)
        {
            if (plugin_handle)
            {
                destroy(plugin_handle);
            }
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            handle = nullptr;
            return false;
        }

        std::shared_ptr<Plugin> plugin = std::make_shared<Plugin>(name, handle, plugin_handle, api, destroy);
        if (!plugin)
        {
            destroy(plugin_handle);
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            return false;
        }

        p_impl->plugins[name] = plugin;

        won::backlog::Post("Succeeded to load plugin : " + name);

        return true;
	}
	bool PluginManager::UnloadPlugin(const String& name)
	{
        std::lock_guard<std::mutex> lock(p_impl->vector_lock);

        auto it = p_impl->plugins.find(name);
        if (it == p_impl->plugins.end())
        {
            won::backlog::Post("Plugin(" + name + ") Already Unloaded.", won::backlog::LogLevel::Warning);
            return false;
        }

        p_impl->plugins.erase(it);

        return true;
	}
	std::shared_ptr<Plugin> PluginManager::GetPlugin(const String& name)
	{
        std::lock_guard<std::mutex> lock(p_impl->vector_lock);

        auto it = p_impl->plugins.find(name);
        if (it != p_impl->plugins.end())
        {
            return it->second;
        }

		return nullptr;
	}
}
