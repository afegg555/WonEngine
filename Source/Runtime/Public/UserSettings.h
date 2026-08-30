#pragma once
#include "Configuration.h"
#include "FileSystem.h"
#include "ViewOptionEnums.h"
#include "Types.h"

#include <optional>

namespace won::settings
{
    inline constexpr uint32 user_settings_version = 1;

    struct UserSettings
    {
        std::optional<bool> vsync;
        std::optional<rendering::AntiAliasingMode> aa_mode;
        std::optional<float> shadow_resolution_scale;
        std::optional<String> language;
    };

    inline String GetUserSettingsPath(const String& app_name)
    {
        if (app_name.empty())
        {
            return String();
        }
        return io::CombinePath(io::CombinePath(io::GetSaveDirectory(app_name), "Config"), "user.cfg");
    }

    inline bool LoadSettings(const String& path, UserSettings& out_settings)
    {
        config::Configuration configuration;
        if (!configuration.LoadFromFile(path.c_str()))
        {
            return false;
        }

        bool bool_value = false;
        float float_value = 0.0f;

        if (configuration.GetBool("vsync", bool_value))
        {
            out_settings.vsync = bool_value;
        }
        if (const char* string_value = configuration.GetString("aa_mode"))
        {
            out_settings.aa_mode = rendering::ParseAntiAliasingMode(string_value);
        }
        if (configuration.GetFloat("shadow_resolution_scale", float_value))
        {
            out_settings.shadow_resolution_scale = float_value;
        }
        if (const char* string_value = configuration.GetString("language"))
        {
            out_settings.language = string_value;
        }
        return true;
    }

    inline bool SaveSettings(const String& path, const UserSettings& settings)
    {
        if (path.empty())
        {
            return false;
        }

        config::Configuration configuration;
        configuration.SetInt("version", static_cast<int>(user_settings_version));
        if (settings.vsync.has_value())
        {
            configuration.SetBool("vsync", settings.vsync.value());
        }
        if (settings.aa_mode.has_value())
        {
            configuration.SetString("aa_mode", rendering::ToString(settings.aa_mode.value()));
        }
        if (settings.shadow_resolution_scale.has_value())
        {
            configuration.SetFloat("shadow_resolution_scale", settings.shadow_resolution_scale.value());
        }
        if (settings.language.has_value())
        {
            configuration.SetString("language", settings.language.value().c_str());
        }

        io::CreateDirectories(io::GetDirectoryFromPath(path));
        return configuration.SaveToFile(path.c_str());
    }
}
