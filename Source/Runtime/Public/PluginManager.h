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

    struct PluginExtension
    {
        String plugin_id;
        String plugin_version;
        PluginType plugin_type = PluginType::Unknown;
        WonExtensionType extension_type = WonExtensionType::Unknown;
        String extension_id;
        void* plugin_handle = nullptr;
        const void* descriptor = nullptr;
    };

    class WONENGINE_API Plugin
    {
    public:
        Plugin(const String& name, PluginType type, void* native_handle, void* plugin_handle, const WonPluginAPI& api, WonPluginDestroyFn destroy);
        ~Plugin();

        Plugin(const Plugin&) = delete;
        Plugin& operator=(const Plugin&) = delete;

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
        String name;
        String plugin_id;
        String plugin_version;
        PluginType plugin_type = PluginType::Unknown;
        void* native_handle = nullptr;
        void* plugin_handle = nullptr;
        WonPluginAPI api = {};
        WonPluginDestroyFn destroy = nullptr;
        Vector<PluginExtension> extensions;
    };

    class WONENGINE_API PluginManager
    {
    public:
        PluginManager();
        ~PluginManager();

        bool LoadPlugin(const String& name);
        bool LoadPluginFromManifest(const String& manifest_path);
        bool UnloadPlugin(const String& name);

        std::shared_ptr<Plugin> GetPlugin(const String& name);
        Vector<PluginExtension> GetExtensions(WonExtensionType extension_type) const;

    private:
        bool LoadPluginBinary(const String& name, const String& library_path, PluginType plugin_type);

        struct Impl;
        Impl* p_impl = nullptr;
    };
}
