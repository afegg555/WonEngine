#include "PluginManager.h"
#include "Backlog.h"
#include "Platform.h"

namespace won::plugin
{
	bool PluginManager::LoadPlugin(const String& name)
	{
		std::lock_guard<std::mutex> lock(vector_lock);

		auto it = plugins.find(name);
		if (it != plugins.end())
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
        plugins[name] = { plugin, handle };

        return true;
	}
	bool PluginManager::UnloadPlugin(const String& name)
	{
        std::lock_guard<std::mutex> lock(vector_lock);

        auto it = plugins.find(name);
        if (it == plugins.end())
        {
            won::backlog::Post("Plugin(" + name + ") Already Unloaded.", won::backlog::LogLevel::Warning);
            return false;
        }

        it->second.plugin = {};
#if defined(_WIN32)
        FreeLibrary((HMODULE)it->second.native_handle);
#endif
        plugins.erase(it);

        return true;
	}
	std::shared_ptr<IPlugin> PluginManager::GetPlugin(const String& name)
	{
        std::lock_guard<std::mutex> lock(vector_lock);

        auto it = plugins.find(name);
        if (it != plugins.end())
        {
            return it->second.plugin;
        }

		return nullptr;
	}
}