#pragma once
#include "PluginABI.h"
#include "RuntimeExport.h"
#include "Types.h"

namespace won::plugin
{
    enum class PluginType : uint32
    {
        Unknown = 0,
        EditorDefault,
        EditorOptional,
        RuntimeOptional,
    };

    struct PluginInfo
    {
        String plugin_id;
        String display_name;
        String version;
        PluginType type = PluginType::Unknown;
        String manifest_path;
        String library_path;
    };

    struct PluginExtension
    {
        WonExtensionType extension_type = WonExtensionType::Unknown;
        String extension_id;
        const void* descriptor = nullptr;
    };

    class WONENGINE_API Plugin
    {
    public:
        Plugin(const PluginInfo& info, void* native_handle, void* plugin_handle, WonPluginDestroyFn destroy);
        ~Plugin();

        Plugin(const Plugin&) = delete;
        Plugin& operator=(const Plugin&) = delete;

        const PluginInfo& GetInfo() const;
        const char* GetName() const;
        const char* GetPluginId() const;
        const char* GetPluginVersion() const;
        PluginType GetPluginType() const;
        void* GetHandle() const;
        bool RegisterExtension(const WonExtensionDesc& desc);
        bool QueryInterface(const char* extension_id, void** out_interface) const;
        void* QueryInterface(const char* extension_id) const;
        const Vector<PluginExtension>& GetExtensions() const;

    private:
        PluginInfo info;
        void* native_handle = nullptr;
        void* plugin_handle = nullptr;
        WonPluginDestroyFn destroy = nullptr;
        Vector<PluginExtension> extensions;
    };

    WONENGINE_API Vector<PluginInfo> ScanPluginList(const String& plugin_root_path);
    WONENGINE_API std::shared_ptr<Plugin> LoadPlugin(const PluginInfo& plugin_info);
}
