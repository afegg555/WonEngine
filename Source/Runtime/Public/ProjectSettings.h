#pragma once
#include "Configuration.h"
#include "FileSystem.h"
#include "RHIDevice.h"
#include "ResourceExtension.h"
#include "Types.h"
#include "PhysicsWorld.h"

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
        bool splash_enabled = true;
        String splash_title = "Won Engine";
        String splash_status = "Starting...";
        String splash_image;
        String input_action_map;
        String game_data_schema;
        rendering::RHIBackend backend_type = rendering::RHIBackend::DirectX12;
        Vector<String> enabled_plugins;
        Vector<String> packaged_scenes;

        // Physics
        uint32 physics_temp_allocator_size     = physics::default_temp_allocator_size;
        uint32 physics_max_bodies              = physics::default_max_bodies;
        uint32 physics_max_body_pairs          = physics::default_max_body_pairs;
        uint32 physics_max_contact_constraints = physics::default_max_contact_constraints;
        float  physics_hz                      = physics::default_physics_hz;
        int    physics_max_steps_per_frame     = physics::default_max_steps_per_frame;
    };

    inline physics::PhysicsWorldDesc GetPhysicsDesc(const ProjectSettings& settings)
    {
        physics::PhysicsWorldDesc desc;
        desc.temp_allocator_size = settings.physics_temp_allocator_size;
        desc.max_bodies = settings.physics_max_bodies;
        desc.max_body_pairs = settings.physics_max_body_pairs;
        desc.max_contact_constraints = settings.physics_max_contact_constraints;
        desc.physics_hz = settings.physics_hz;
        desc.max_steps_per_frame = settings.physics_max_steps_per_frame;
        return desc;
    }

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
        if (configuration.GetBool("splash_enabled", bool_value))
        {
            settings.splash_enabled = bool_value;
        }
        if (const char* string_value = configuration.GetString("splash_title"))
        {
            settings.splash_title = string_value;
        }
        if (const char* string_value = configuration.GetString("splash_status"))
        {
            settings.splash_status = string_value;
        }
        if (const char* string_value = configuration.GetString("splash_image"))
        {
            settings.splash_image = string_value;
        }
        if (const char* string_value = configuration.GetString("input_action_map"))
        {
            settings.input_action_map = string_value;
        }
        if (const char* string_value = configuration.GetString("game_data_schema"))
        {
            settings.game_data_schema = string_value;
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
        auto parse_semicolon_list = [&configuration](const char* key, Vector<String>& out)
        {
            out.clear();
            if (const char* string_value = configuration.GetString(key))
            {
                String value = string_value;
                Size start_pos = 0;
                while (start_pos <= value.size())
                {
                    const Size sep = value.find(';', start_pos);
                    String item = sep == String::npos ? value.substr(start_pos) : value.substr(start_pos, sep - start_pos);
                    if (!item.empty())
                        out.push_back(item);
                    if (sep == String::npos)
                        break;
                    start_pos = sep + 1;
                }
            }
        };
        parse_semicolon_list("enabled_plugins", settings.enabled_plugins);
        parse_semicolon_list("packaged_scenes", settings.packaged_scenes);

        float float_value = 0.0f;
        if (configuration.GetInt("physics_temp_allocator_size", int_value))
            settings.physics_temp_allocator_size = static_cast<uint32>(int_value);
        if (configuration.GetInt("physics_max_bodies", int_value))
            settings.physics_max_bodies = static_cast<uint32>(int_value);
        if (configuration.GetInt("physics_max_body_pairs", int_value))
            settings.physics_max_body_pairs = static_cast<uint32>(int_value);
        if (configuration.GetInt("physics_max_contact_constraints", int_value))
            settings.physics_max_contact_constraints = static_cast<uint32>(int_value);
        if (configuration.GetFloat("physics_hz", float_value))
            settings.physics_hz = float_value;
        if (configuration.GetInt("physics_max_steps_per_frame", int_value))
            settings.physics_max_steps_per_frame = int_value;

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
        configuration.SetBool("splash_enabled", settings.splash_enabled);
        configuration.SetString("splash_title", settings.splash_title.c_str());
        configuration.SetString("splash_status", settings.splash_status.c_str());
        configuration.SetString("splash_image", settings.splash_image.c_str());
        configuration.SetString("input_action_map", settings.input_action_map.c_str());
        configuration.SetString("game_data_schema", settings.game_data_schema.c_str());

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
        auto serialize_semicolon_list = [](const Vector<String>& items) -> String
        {
            String result;
            for (Size i = 0; i < items.size(); ++i)
            {
                if (i > 0) result += ";";
                result += items[i];
            }
            return result;
        };
        configuration.SetString("enabled_plugins", serialize_semicolon_list(settings.enabled_plugins).c_str());
        configuration.SetString("packaged_scenes", serialize_semicolon_list(settings.packaged_scenes).c_str());

        configuration.SetInt("physics_temp_allocator_size", static_cast<int>(settings.physics_temp_allocator_size));
        configuration.SetInt("physics_max_bodies",              static_cast<int>(settings.physics_max_bodies));
        configuration.SetInt("physics_max_body_pairs",          static_cast<int>(settings.physics_max_body_pairs));
        configuration.SetInt("physics_max_contact_constraints", static_cast<int>(settings.physics_max_contact_constraints));
        configuration.SetFloat("physics_hz",                   settings.physics_hz);
        configuration.SetInt("physics_max_steps_per_frame",    settings.physics_max_steps_per_frame);

        return configuration.SaveToFile(path.c_str());
    }
}
