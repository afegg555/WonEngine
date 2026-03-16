#include "PluginManager.h"
#include "Backlog.h"
#include "Platform.h"

namespace won::plugin
{
    struct PluginManager::Impl
    {
        struct PluginHandle
        {
            std::shared_ptr<IPlugin> plugin;
            void* native_handle;
        };

        UnorderedMap<String, PluginHandle> plugins;
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
                if (entry.second.native_handle)
                {
                    entry.second.plugin->Shutdown();
                    entry.second.plugin.reset();
#if defined(_WIN32)
                    FreeLibrary((HMODULE)entry.second.native_handle);
#endif
                }
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

        CreatePluginFn creater = nullptr;
#if defined(_WIN32)
        creater = reinterpret_cast<CreatePluginFn>(
            GetProcAddress((HMODULE)handle, "CreatePlugin")
            );
#endif

        if (!creater)
        {
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            handle = nullptr;
            return false;
        }

        std::shared_ptr<IPlugin> plugin{ creater() };

        if (!plugin)
        {
            return false;
        }

        plugin->Initialize();
        p_impl->plugins[name] = { plugin, handle };

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

        it->second.plugin = {};
#if defined(_WIN32)
        FreeLibrary((HMODULE)it->second.native_handle);
#endif
        p_impl->plugins.erase(it);

        return true;
	}
	std::shared_ptr<IPlugin> PluginManager::GetPlugin(const String& name)
	{
        std::lock_guard<std::mutex> lock(p_impl->vector_lock);

        auto it = p_impl->plugins.find(name);
        if (it != p_impl->plugins.end())
        {
            return it->second.plugin;
        }

		return nullptr;
	}
}