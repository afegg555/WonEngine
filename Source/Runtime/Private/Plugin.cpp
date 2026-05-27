#include "Plugin.h"
#include "Backlog.h"
#include "FileSystem.h"
#include "Platform.h"

#include <algorithm>
#include <cstring>
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

        PluginType ParsePluginType(const String& plugin_type_name)
        {
            if (plugin_type_name == "EditorDefault")
            {
                return PluginType::EditorDefault;
            }
            if (plugin_type_name == "EditorOptional")
            {
                return PluginType::EditorOptional;
            }
            if (plugin_type_name == "RuntimeOptional")
            {
                return PluginType::RuntimeOptional;
            }
            return PluginType::Unknown;
        }

        bool ReadPluginManifest(const String& manifest_path, PluginInfo* out_info)
        {
            if (!out_info)
            {
                return false;
            }

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

            if (!manifest.contains("plugin_id") || !manifest["plugin_id"].is_string() || !manifest.contains("type") || !manifest["type"].is_string())
            {
                backlog::Post("Invalid plugin manifest : " + manifest_path, backlog::LogLevel::Warning);
                return false;
            }

            PluginInfo info = {};
            info.plugin_id = manifest["plugin_id"].get<String>();
            if (info.plugin_id.empty())
            {
                backlog::Post("Invalid plugin id : " + manifest_path, backlog::LogLevel::Warning);
                return false;
            }

            info.display_name = manifest.contains("display_name") && manifest["display_name"].is_string() ? manifest["display_name"].get<String>() : info.plugin_id;
            info.version = manifest.contains("version") && manifest["version"].is_string() ? manifest["version"].get<String>() : "";
            info.type = ParsePluginType(manifest["type"].get<String>());
            info.manifest_path = manifest_path;
            if (info.type == PluginType::Unknown)
            {
                backlog::Post("Invalid plugin type : " + manifest_path, backlog::LogLevel::Warning);
                return false;
            }

            if (!manifest.contains("libraries") || !manifest["libraries"].is_object())
            {
                backlog::Post("Invalid plugin library manifest : " + manifest_path, backlog::LogLevel::Warning);
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

            if (platform_key[0] == '\0' || !manifest["libraries"].contains(platform_key) || !manifest["libraries"][platform_key].is_object() ||
                !manifest["libraries"][platform_key].contains(library_key) || !manifest["libraries"][platform_key][library_key].is_string())
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

            info.library_path = io::IsAbsolutePath(library_path) ? io::NormalizePath(library_path) : io::CombinePath(io::GetDirectoryFromPath(manifest_path), library_path);

            *out_info = info;
            return true;
        }
    }

    Plugin::Plugin(const PluginInfo& info_in, void* native_handle_in, void* plugin_handle_in, WonPluginDestroyFn destroy_in)
        : info(info_in)
        , native_handle(native_handle_in)
        , plugin_handle(plugin_handle_in)
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

    const PluginInfo& Plugin::GetInfo() const
    {
        return info;
    }

    const char* Plugin::GetName() const
    {
        return info.plugin_id.c_str();
    }

    const char* Plugin::GetPluginId() const
    {
        return info.plugin_id.c_str();
    }

    const char* Plugin::GetPluginVersion() const
    {
        return info.version.c_str();
    }

    PluginType Plugin::GetPluginType() const
    {
        return info.type;
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
        extension.extension_type = desc.extension_type;
        extension.extension_id = desc.extension_id;
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

    Vector<PluginInfo> ScanPluginList(const String& plugin_root_path)
    {
        Vector<PluginInfo> plugin_list;
        Vector<io::DirectoryEntry> entries;
        const String root_path = io::GetAbsolutePath(plugin_root_path);
        if (!io::EnumerateDirectoryRecursive(root_path, &entries))
        {
            return plugin_list;
        }

        for (const io::DirectoryEntry& entry : entries)
        {
            if (!entry.is_file || io::GetFilename(entry.path) != "plugin.json")
            {
                continue;
            }

            PluginInfo plugin_info = {};
            if (!ReadPluginManifest(entry.path, &plugin_info))
            {
                continue;
            }

            bool duplicated = false;
            for (const PluginInfo& existing : plugin_list)
            {
                if (existing.plugin_id == plugin_info.plugin_id)
                {
                    duplicated = true;
                    break;
                }
            }
            if (duplicated)
            {
                continue;
            }

            plugin_list.push_back(plugin_info);
        }

        std::sort(plugin_list.begin(), plugin_list.end(), [](const PluginInfo& lhs, const PluginInfo& rhs)
        {
            if (lhs.type != rhs.type)
            {
                return static_cast<uint32>(lhs.type) < static_cast<uint32>(rhs.type);
            }
            return lhs.display_name < rhs.display_name;
        });

        return plugin_list;
    }

    std::shared_ptr<Plugin> LoadPlugin(const PluginInfo& plugin_info)
    {
        if (plugin_info.plugin_id.empty() || plugin_info.library_path.empty())
        {
            return nullptr;
        }

        const String resolved_library_path = io::GetAbsolutePath(plugin_info.library_path);
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
            won::backlog::Post("Failed to load plugin : " + plugin_info.plugin_id + " (" + resolved_library_path + "), error=" + std::to_string(error_code));
#else
            won::backlog::Post("Failed to load plugin : " + plugin_info.plugin_id + " (" + resolved_library_path + ")");
#endif
            return nullptr;
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
            return nullptr;
        }

        void* plugin_handle = nullptr;
        WonPluginAPI api = {};
        static WonPluginHostAPI host_api = MakeHostAPI();
        const bool created = create(&host_api, &plugin_handle, &api);
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
            return nullptr;
        }
        if (std::strcmp(api.plugin_id, plugin_info.plugin_id.c_str()) != 0)
        {
            destroy(plugin_handle);
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            handle = nullptr;
            return nullptr;
        }

        PluginInfo loaded_info = plugin_info;
        if (loaded_info.display_name.empty())
        {
            loaded_info.display_name = loaded_info.plugin_id;
        }
        if (loaded_info.version.empty())
        {
            loaded_info.version = api.plugin_version;
        }

        std::shared_ptr<Plugin> plugin = std::make_shared<Plugin>(loaded_info, handle, plugin_handle, destroy);
        if (!plugin)
        {
            destroy(plugin_handle);
#if defined(_WIN32)
            FreeLibrary((HMODULE)handle);
#endif
            return nullptr;
        }

        for (uint32 extension_index = 0; extension_index < api.extension_count; ++extension_index)
        {
            if (!plugin->RegisterExtension(api.extensions[extension_index]))
            {
                plugin.reset();
                return nullptr;
            }
        }

        won::backlog::Post("Succeeded to load plugin : " + plugin_info.plugin_id);
        return plugin;
    }
}
