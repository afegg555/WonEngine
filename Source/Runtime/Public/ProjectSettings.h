#pragma once
#include "Configuration.h"
#include "FileSystem.h"
#include "RHIDevice.h"
#include "Types.h"

namespace won::project
{
    inline constexpr uint32 project_settings_version = 1;
    inline constexpr const char* project_file_extension = "wonproj";
    inline constexpr const char* default_project_file_name = "Project.wonproj";
    inline constexpr const char* content_virtual_root = "/Contents";
    inline constexpr const char* content_virtual_root_prefix = "/Contents/";
    inline constexpr Size content_virtual_root_prefix_length = sizeof("/Contents/") - 1;

    struct ProjectSettings
    {
        uint32 version = project_settings_version;
        String settings_path;
        String project_root;
        String project_name;
        String content_root = "Contents";
        String startup_scene;
        String window_title = "WonEngine";
        int window_width = 1280;
        int window_height = 720;
        bool window_fullscreen = false;
        bool window_resizable = true;
        bool window_use_title_bar = true;
        bool window_visible = true;
        bool vsync_enabled = true;
        rendering::RHIBackend backend_type = rendering::RHIBackend::DirectX12;
        Vector<String> enabled_plugins;
    };

    inline String GetContentRoot(const ProjectSettings& settings)
    {
        String content_root = settings.content_root.empty() ? "Contents" : settings.content_root;
        if (!io::IsAbsolutePath(content_root))
        {
            content_root = io::CombinePath(settings.project_root, content_root);
        }
        return io::NormalizePath(content_root);
    }

    inline String ResolveProjectContentPath(const String& content_root, const String& path)
    {
        if (path.empty())
        {
            return String();
        }
        if (io::IsAbsolutePath(path))
        {
            return io::NormalizePath(path);
        }
        if (path == content_virtual_root)
        {
            return io::NormalizePath(content_root);
        }
        if (path.rfind(content_virtual_root_prefix, 0) == 0)
        {
            return io::NormalizePath(io::CombinePath(content_root, path.substr(content_virtual_root_prefix_length)));
        }
        return io::NormalizePath(io::CombinePath(content_root, path));
    }

    inline bool LoadSettings(const String& path, ProjectSettings& out_settings)
    {
        ProjectSettings settings = out_settings;
        settings.settings_path = io::NormalizePath(path);
        const String settings_directory = io::GetDirectoryFromPath(settings.settings_path);
        if (settings.project_root.empty())
        {
            settings.project_root = settings_directory;
        }
        else if (!io::IsAbsolutePath(settings.project_root))
        {
            settings.project_root = io::CombinePath(settings_directory, settings.project_root);
        }
        settings.project_root = io::NormalizePath(settings.project_root);

        config::Configuration configuration;
        if (!configuration.LoadFromFile(path.c_str()))
        {
            out_settings = settings;
            return false;
        }

        int int_value = 0;
        bool bool_value = false;

        if (configuration.GetInt("version", int_value))
        {
            settings.version = static_cast<uint32>(int_value);
        }
        if (const char* string_value = configuration.GetString("project_name"))
        {
            settings.project_name = string_value;
        }
        if (const char* string_value = configuration.GetString("content_root"))
        {
            settings.content_root = string_value;
        }
        if (const char* string_value = configuration.GetString("startup_scene"))
        {
            settings.startup_scene = string_value;
        }
        if (const char* string_value = configuration.GetString("window_title"))
        {
            settings.window_title = string_value;
        }
        if (configuration.GetInt("window_width", int_value))
        {
            settings.window_width = int_value;
        }
        if (configuration.GetInt("window_height", int_value))
        {
            settings.window_height = int_value;
        }
        if (configuration.GetBool("window_fullscreen", bool_value))
        {
            settings.window_fullscreen = bool_value;
        }
        if (configuration.GetBool("window_resizable", bool_value))
        {
            settings.window_resizable = bool_value;
        }
        if (configuration.GetBool("window_use_title_bar", bool_value))
        {
            settings.window_use_title_bar = bool_value;
        }
        if (configuration.GetBool("window_visible", bool_value))
        {
            settings.window_visible = bool_value;
        }
        if (configuration.GetBool("vsync_enabled", bool_value))
        {
            settings.vsync_enabled = bool_value;
        }
        if (const char* string_value = configuration.GetString("backend_type"))
        {
            String backend_type = string_value;
            if (backend_type == "Vulkan")
            {
                settings.backend_type = rendering::RHIBackend::Vulkan;
            }
            else if (backend_type == "Metal")
            {
                settings.backend_type = rendering::RHIBackend::Metal;
            }
            else
            {
                settings.backend_type = rendering::RHIBackend::DirectX12;
            }
        }
        settings.enabled_plugins.clear();
        if (const char* string_value = configuration.GetString("enabled_plugins"))
        {
            String enabled_plugins_value = string_value;
            Size start_pos = 0;
            while (start_pos <= enabled_plugins_value.size())
            {
                const Size separator_pos = enabled_plugins_value.find(';', start_pos);
                String plugin_id = separator_pos == String::npos ? enabled_plugins_value.substr(start_pos) : enabled_plugins_value.substr(start_pos, separator_pos - start_pos);
                if (!plugin_id.empty())
                {
                    settings.enabled_plugins.push_back(plugin_id);
                }
                if (separator_pos == String::npos)
                {
                    break;
                }
                start_pos = separator_pos + 1;
            }
        }

        out_settings = settings;
        return settings.version == project_settings_version;
    }

    inline bool SaveSettings(const String& path, const ProjectSettings& settings)
    {
        config::Configuration configuration;
        configuration.SetInt("version", static_cast<int>(project_settings_version));
        configuration.SetString("project_name", settings.project_name.c_str());
        configuration.SetString("content_root", settings.content_root.c_str());
        configuration.SetString("startup_scene", settings.startup_scene.c_str());
        configuration.SetString("window_title", settings.window_title.c_str());
        configuration.SetInt("window_width", settings.window_width);
        configuration.SetInt("window_height", settings.window_height);
        configuration.SetBool("window_fullscreen", settings.window_fullscreen);
        configuration.SetBool("window_resizable", settings.window_resizable);
        configuration.SetBool("window_use_title_bar", settings.window_use_title_bar);
        configuration.SetBool("window_visible", settings.window_visible);
        configuration.SetBool("vsync_enabled", settings.vsync_enabled);

        String backend_type = "DirectX12";
        if (settings.backend_type == rendering::RHIBackend::Vulkan)
        {
            backend_type = "Vulkan";
        }
        else if (settings.backend_type == rendering::RHIBackend::Metal)
        {
            backend_type = "Metal";
        }
        configuration.SetString("backend_type", backend_type.c_str());
        String enabled_plugins;
        for (Size plugin_index = 0; plugin_index < settings.enabled_plugins.size(); ++plugin_index)
        {
            if (plugin_index > 0)
            {
                enabled_plugins += ";";
            }
            enabled_plugins += settings.enabled_plugins[plugin_index];
        }
        configuration.SetString("enabled_plugins", enabled_plugins.c_str());
        return configuration.SaveToFile(path.c_str());
    }
}
