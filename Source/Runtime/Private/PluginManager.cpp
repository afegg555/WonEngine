#include "PluginManager.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "Platform.h"

#include <cstring>
#include <mutex>
#include <nlohmann/json.hpp>

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

        WonPluginHostAPI MakeHostAPI()
        {
            WonPluginHostAPI host_api = {};
            host_api.abi_version = WON_PLUGIN_ABI_VERSION;
            host_api.Log = HostLog;
            return host_api;
        }
    }

    Plugin::Plugin(const String& name_in, PluginType type_in, void* native_handle_in, void* plugin_handle_in, const WonPluginAPI& api_in, WonPluginDestroyFn destroy_in)
        : name(name_in)
        , plugin_type(type_in)
        , native_handle(native_handle_in)
        , plugin_handle(plugin_handle_in)
        , api(api_in)
        , destroy(destroy_in)
    {
        plugin_id = api.plugin_id ? api.plugin_id : "";
        plugin_version = api.plugin_version ? api.plugin_version : "";
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

    const char* Plugin::GetPluginId() const
    {
        return plugin_id.c_str();
    }

    const char* Plugin::GetPluginVersion() const
    {
        return plugin_version.c_str();
    }

    PluginType Plugin::GetPluginType() const
    {
        return plugin_type;
    }

    void* Plugin::GetHandle() const
    {
        return plugin_handle;
    }

    bool Plugin::RegisterExtension(const WonExtensionDesc& desc)
    {
        if (desc.struct_size < sizeof(WonExtensionDesc) || !desc.extension_id || !desc.descriptor)
        {
            return false;
        }
        if (desc.extension_type != WonExtensionType::Unknown &&
            desc.extension_type != WonExtensionType::Function &&
            desc.extension_type != WonExtensionType::Component &&
            desc.extension_type != WonExtensionType::System)
        {
            return false;
        }

        PluginExtension extension = {};
        extension.plugin_id = plugin_id;
        extension.plugin_version = plugin_version;
        extension.plugin_type = plugin_type;
        extension.extension_type = desc.extension_type;
        extension.extension_id = desc.extension_id;
        extension.plugin_handle = plugin_handle;
        extension.descriptor = desc.descriptor;
        extensions.push_back(extension);
        return true;
    }

    bool Plugin::QueryInterface(const char* extension_id, void** out_interface) const
    {
        if (!plugin_handle || !extension_id || !out_interface)
        {
            return false;
        }

        *out_interface = nullptr;
        for (const PluginExtension& extension : extensions)
        {
            if (extension.extension_id == extension_id)
            {
                *out_interface = const_cast<void*>(extension.descriptor);
                return true;
            }
        }

        return false;
    }

    void* Plugin::QueryInterface(const char* extension_id) const
    {
        void* result = nullptr;
        QueryInterface(extension_id, &result);
        return result;
    }

    const Vector<PluginExtension>& Plugin::GetExtensions() const
    {
        return extensions;
    }

    struct PluginManager::Impl
    {
        UnorderedMap<String, std::shared_ptr<Plugin>> plugins;
        std::mutex vector_lock;
        WonPluginHostAPI host_api = MakeHostAPI();
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
        String file_name = name;
#if defined(_DEBUG)
        file_name += "d";
#endif
#if defined(_WIN32)
        file_name += ".dll";
#endif

        return LoadPluginBinary(name, file_name, PluginType::Unknown);
	}

    bool PluginManager::LoadPluginFromManifest(const String& manifest_path)
    {
        io::FileData manifest_file;
        if (!io::ReadAllBytes(manifest_path, &manifest_file))
        {
            backlog::Post("Failed to open plugin manifest : " + manifest_path, backlog::LogLevel::Warning);
            return false;
        }

        nlohmann::json manifest;
        try
        {
            manifest = nlohmann::json::parse(manifest_file.bytes.begin(), manifest_file.bytes.end());
        }
        catch (const nlohmann::json::exception&)
        {
            backlog::Post("Invalid plugin manifest json : " + manifest_path, backlog::LogLevel::Warning);
            return false;
        }

        if (!manifest.contains("plugin_id") || !manifest["plugin_id"].is_string() ||
            !manifest.contains("type") || !manifest["type"].is_string() ||
            !manifest.contains("libraries") || !manifest["libraries"].is_object())
        {
            backlog::Post("Invalid plugin manifest : " + manifest_path, backlog::LogLevel::Warning);
            return false;
        }

        String plugin_id = manifest["plugin_id"].get<String>();
        const String plugin_type_name = manifest["type"].get<String>();
        PluginType plugin_type = PluginType::Unknown;
        if (plugin_type_name == "EditorDefault")
        {
            plugin_type = PluginType::EditorDefault;
        }
        else if (plugin_type_name == "EditorOptional")
        {
            plugin_type = PluginType::EditorOptional;
        }
        else if (plugin_type_name == "RuntimeOptional")
        {
            plugin_type = PluginType::RuntimeOptional;
        }
        if (plugin_type == PluginType::Unknown)
        {
            backlog::Post("Invalid plugin type : " + manifest_path, backlog::LogLevel::Warning);
            return false;
        }

#if defined(_WIN32)
    #if defined(_DEBUG)
        constexpr const char* library_key = "debug";
    #else
        constexpr const char* library_key = "release";
    #endif
        constexpr const char* platform_key = "windows";
#else
        constexpr const char* library_key = "";
        constexpr const char* platform_key = "";
#endif

        if (platform_key[0] == '\0' ||
            !manifest["libraries"].contains(platform_key) ||
            !manifest["libraries"][platform_key].is_object() ||
            !manifest["libraries"][platform_key].contains(library_key) ||
            !manifest["libraries"][platform_key][library_key].is_string())
        {
            backlog::Post("Invalid plugin library manifest : " + manifest_path, backlog::LogLevel::Warning);
            return false;
        }

        String library_path = manifest["libraries"][platform_key][library_key].get<String>();
        if (library_path.empty())
        {
            backlog::Post("Invalid plugin library path : " + manifest_path, backlog::LogLevel::Warning);
            return false;
        }

        String resolved_library_path = io::IsAbsolutePath(library_path) ? io::NormalizePath(library_path) : io::CombinePath(io::GetDirectoryFromPath(manifest_path), library_path);
        return LoadPluginBinary(plugin_id, resolved_library_path, plugin_type);
    }

    bool PluginManager::LoadPluginBinary(const String& name, const String& library_path, PluginType plugin_type)
    {
        std::lock_guard<std::mutex> lock(p_impl->vector_lock);

        auto it = p_impl->plugins.find(name);
        if (it != p_impl->plugins.end())
        {
            won::backlog::Post("Plugin(" + name + ") Already Loaded.", won::backlog::LogLevel::Warning);
            return false;
        }

        const String resolved_library_path = io::GetAbsolutePath(library_path);
        void* handle = nullptr;
#if defined(_WIN32)
        String native_library_path = resolved_library_path;
        std::replace(native_library_path.begin(), native_library_path.end(), '/', '\\'); // why not work with '/'?
        const DWORD load_flags = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
        handle = (void*)::LoadLibraryExA(native_library_path.c_str(), nullptr, load_flags);
#endif

        if (!handle)
        {
#if defined(_WIN32)
            const DWORD error_code = GetLastError();
            won::backlog::Post("Failed to load plugin : " + name + " (" + resolved_library_path + "), error=" + std::to_string(error_code));
#else
            won::backlog::Post("Failed to load plugin : " + name + " (" + resolved_library_path + ")");
#endif
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

        void* plugin_handle = nullptr;
        WonPluginAPI api = {};
        const bool created = create(&p_impl->host_api, &plugin_handle, &api);
        if (!created || !plugin_handle || api.abi_version != WON_PLUGIN_ABI_VERSION || !api.plugin_id || !api.plugin_version ||
            (api.extension_count > 0 && !api.extensions))
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
        if (std::strcmp(api.plugin_id, name.c_str()) != 0)
        {
            destroy(plugin_handle);
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            handle = nullptr;
            return false;
        }

        std::shared_ptr<Plugin> plugin = std::make_shared<Plugin>(name, plugin_type, handle, plugin_handle, api, destroy);
        if (!plugin)
        {
            destroy(plugin_handle);
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            return false;
        }

        for (uint32 extension_index = 0; extension_index < api.extension_count; ++extension_index)
        {
            if (!plugin->RegisterExtension(api.extensions[extension_index]))
            {
                plugin.reset();
                return false;
            }
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

    Vector<PluginExtension> PluginManager::GetExtensions(WonExtensionType extension_type) const
    {
        Vector<PluginExtension> result;
        std::lock_guard<std::mutex> lock(p_impl->vector_lock);
        for (const auto& entry : p_impl->plugins)
        {
            const std::shared_ptr<Plugin>& plugin = entry.second;
            if (!plugin)
            {
                continue;
            }
            for (const PluginExtension& extension : plugin->GetExtensions())
            {
                if (extension.extension_type == extension_type)
                {
                    result.push_back(extension);
                }
            }
        }
        return result;
    }
}
