#pragma once
#include "Configuration.h"
#include "FileSystem.h"
#include "Types.h"
#include "ViewOptionEnums.h"

namespace won::editor
{
    inline constexpr const char* editor_settings_file_name = "EditorSettings.json";
    inline constexpr const char* editor_default_language = "en";
    inline constexpr const char* editor_layout_file_name = "EditorLayout.ini";

    struct ShowFlagSetting
    {
        uint32 flag;
        const char* key;
    };

    inline constexpr ShowFlagSetting show_flag_settings[] = {
        { rendering::Show_Opaque,      "editor.viewport.show.opaque" },
        { rendering::Show_Transparent, "editor.viewport.show.transparent" },
        { rendering::Show_Decals,      "editor.viewport.show.decals" },
        { rendering::Show_Water,       "editor.viewport.show.water" },
        { rendering::Show_Particles,   "editor.viewport.show.particles" },
        { rendering::Show_Sprites3D,   "editor.viewport.show.sprites_3d" },
        { rendering::Show_Sprites2D,   "editor.viewport.show.sprites_2d" },
        { rendering::Show_Shadows,     "editor.viewport.show.shadows" },
        { rendering::Show_Grid,        "editor.viewport.show.grid" },
        { rendering::Show_Colliders,   "editor.viewport.show.colliders" },
        { rendering::Show_BVH,         "editor.viewport.show.bvh" },
        { rendering::Show_DDGI,        "editor.viewport.show.ddgi" },
    };

    struct EditorSettings
    {
        String settings_path;
        String content_current_folder = "/Contents";
        int content_type_filter = 0;
        float content_tile_size = 72.0f;
        uint32 viewport_show_flags = rendering::Show_Default | rendering::Show_Colliders;
        int viewport_view_mode = 0;
        float camera_speed = 5.0f;
        bool editor_camera_auto_exposure = true;
        float editor_camera_fixed_ev100 = 10.965784f;
        float editor_camera_exposure_compensation = 0.0f;
        float editor_camera_auto_exposure_min_ev = -6.0f;
        float editor_camera_auto_exposure_max_ev = 16.0f;
        float editor_camera_auto_exposure_speed = 2.0f;
        String last_scene_path;
        String editor_language;
    };

    inline bool LoadSettings(const String& path, EditorSettings& out_settings)
    {
        EditorSettings settings = {};
        settings.settings_path = io::NormalizePath(path);

        config::Configuration configuration;
        if (!configuration.LoadFromFile(path.c_str()))
        {
            out_settings = settings;
            return false;
        }

        int int_value = 0;
        float float_value = 0.0f;
        bool bool_value = false;

        if (const char* string_value = configuration.GetString("editor.content.current_folder"))
        {
            settings.content_current_folder = string_value;
        }
        if (configuration.GetInt("editor.content.type_filter", int_value))
        {
            settings.content_type_filter = int_value;
        }
        if (configuration.GetFloat("editor.content.tile_size", float_value))
        {
            settings.content_tile_size = float_value;
        }
        for (const ShowFlagSetting& item : show_flag_settings)
        {
            if (!configuration.GetBool(item.key, bool_value))
            {
                continue;
            }
            if (bool_value)
            {
                settings.viewport_show_flags |= item.flag;
            }
            else
            {
                settings.viewport_show_flags &= ~item.flag;
            }
        }
        if (configuration.GetInt("editor.viewport.view_mode", int_value))
        {
            settings.viewport_view_mode = int_value;
        }
        if (configuration.GetFloat("editor.camera.speed", float_value))
        {
            settings.camera_speed = float_value;
        }
        if (configuration.GetBool("editor.camera.auto_exposure", bool_value))
        {
            settings.editor_camera_auto_exposure = bool_value;
        }
        if (configuration.GetFloat("editor.camera.fixed_ev100", float_value))
        {
            settings.editor_camera_fixed_ev100 = float_value;
        }
        if (configuration.GetFloat("editor.camera.exposure_compensation", float_value))
        {
            settings.editor_camera_exposure_compensation = float_value;
        }
        if (configuration.GetFloat("editor.camera.auto_exposure_min_ev", float_value))
        {
            settings.editor_camera_auto_exposure_min_ev = float_value;
        }
        if (configuration.GetFloat("editor.camera.auto_exposure_max_ev", float_value))
        {
            settings.editor_camera_auto_exposure_max_ev = float_value;
        }
        if (configuration.GetFloat("editor.camera.auto_exposure_speed", float_value))
        {
            settings.editor_camera_auto_exposure_speed = float_value;
        }
        if (const char* string_value = configuration.GetString("editor.scene.last_path"))
        {
            settings.last_scene_path = string_value;
        }
        if (const char* string_value = configuration.GetString("editor.language"))
        {
            settings.editor_language = string_value;
        }

        out_settings = settings;
        return true;
    }

    inline bool SaveSettings(const String& path, const EditorSettings& settings)
    {
        config::Configuration configuration;
        configuration.SetString("editor.content.current_folder", settings.content_current_folder.c_str());
        configuration.SetInt("editor.content.type_filter", settings.content_type_filter);
        configuration.SetFloat("editor.content.tile_size", settings.content_tile_size);
        for (const ShowFlagSetting& item : show_flag_settings)
        {
            configuration.SetBool(item.key, (settings.viewport_show_flags & item.flag) != 0);
        }
        configuration.SetInt("editor.viewport.view_mode", settings.viewport_view_mode);
        configuration.SetFloat("editor.camera.speed", settings.camera_speed);
        configuration.SetBool("editor.camera.auto_exposure", settings.editor_camera_auto_exposure);
        configuration.SetFloat("editor.camera.fixed_ev100", settings.editor_camera_fixed_ev100);
        configuration.SetFloat("editor.camera.exposure_compensation", settings.editor_camera_exposure_compensation);
        configuration.SetFloat("editor.camera.auto_exposure_min_ev", settings.editor_camera_auto_exposure_min_ev);
        configuration.SetFloat("editor.camera.auto_exposure_max_ev", settings.editor_camera_auto_exposure_max_ev);
        configuration.SetFloat("editor.camera.auto_exposure_speed", settings.editor_camera_auto_exposure_speed);
        configuration.SetString("editor.scene.last_path", settings.last_scene_path.c_str());
        configuration.SetString("editor.language", settings.editor_language.c_str());
        return configuration.SaveToFile(path.c_str());
    }
}
