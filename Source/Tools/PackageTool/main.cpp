#include "Configuration.h"
#include "FileSystem.h"
#include "JsonArchive.h"
#include "ProjectSettings.h"
#include "ResourceExtension.h"

#include <cstdlib>
#include <iostream>
#include <string>

#define WONENGINE_PACKAGE_REFERENCED_ASSETS_ONLY

int main(int argc, char** argv)
{
    // currently Windows only.
    // TODO: support other platforms
#ifdef WONENGINE_PACKAGE_REFERENCED_ASSETS_ONLY
    constexpr bool referenced_assets_only = true;
#else
    constexpr bool referenced_assets_only = false;
#endif

    won::config::Configuration arguments;
    arguments.LoadFromCommandLine(argc, argv);

    // PackageTool Projects/ScriptedTriangle
    // PackageTool Projects/ScriptedTriangle/ScriptedTriangle.wonproj
    // PackageTool Projects/ScriptedTriangle/ScriptedTriangle.wonproj Debug

    const won::String target = "Player"; // TODO: support Player variants (ex. PackageTool Projects/ScriptedTriangle/ScriptedTriangle.wonproj """"VRPlayer"""")
    const char* project_path_arg = arguments.GetString("0");
    const char* config_arg = arguments.GetString("1");
    const char* manifest_arg = arguments.GetString("2");
    const char* check_extra = arguments.GetString("3");

    if (project_path_arg == nullptr || project_path_arg[0] == '\0' || check_extra != nullptr)
    {
        std::cout << "Usage: PackageTool project_path [Debug|Release] [--manifest]\n";
        return 1;
    }

    bool generate_manifest = false;
    if (manifest_arg != nullptr)
    {
        if (won::String(manifest_arg) != "--manifest")
        {
            std::cout << "Invalid argument: " << manifest_arg << "\n";
            std::cout << "Usage: PackageTool project_path [Debug|Release] [--manifest]\n";
            return 1;
        }
        generate_manifest = true;
    }

    won::String config = "Release";
    if (config_arg != nullptr)
    {
        if (won::String(config_arg) != "Debug" && won::String(config_arg) != "Release")
        {
            std::cout << "Invalid build config: " << config_arg << "\n";
            return 1;
        }
        config = config_arg;
    }
    const bool copy_debug_info = config == "Debug";

    const won::String engine_root = won::io::NormalizePath(ENGINE_ROOT_DIR);
    const won::String project_path = won::io::GetAbsolutePath(project_path_arg);
    won::String source_settings_path = project_path;
    won::String project_root = won::io::GetDirectoryFromPath(source_settings_path);
    if (!won::io::IsFile(source_settings_path))
    {
        project_root = project_path;
        source_settings_path = won::io::CombinePath(project_root, won::io::ReplaceExtension(won::io::GetFilename(project_root), won::project::project_file_extension));
        if (!won::io::IsFile(source_settings_path))
        {
            source_settings_path = won::io::CombinePath(project_root, won::project::default_project_file_name);
        }
    }
    const won::String build_bat = won::io::CombinePath(engine_root, "Build_Windows.bat");
    const won::String binary_root = won::io::CombinePath(won::io::CombinePath(engine_root, "Binary"), "Windows");
    const won::String packages_root = won::io::CombinePath(binary_root, "Packages");

    std::cout << "Target: " << target << "\n";
    std::cout << "Config: " << config << "\n";
    std::cout << "Engine: " << engine_root << "\n";
    std::cout << "Project: " << project_root << "\n";
    std::cout << "Project file: " << source_settings_path << "\n";

    // build Player & ShaderOfflineCompiler
    const won::String build_commands[] =
    {
        "cmd /c \"\"" + build_bat + "\" \"" + target + "\" \"" + config + "\"\"",
        "cmd /c \"\"" + build_bat + "\" ShaderOfflineCompiler \"" + config + "\"\""
    };
    const char* build_error_messages[] = { "Target build failed.\n", "ShaderOfflineCompiler build failed.\n" };
    for (int build_index = 0; build_index < 2; ++build_index)
    {
        if (std::system(build_commands[build_index].c_str()) != 0)
        {
            std::cout << build_error_messages[build_index];
            return 1;
        }
    }

    won::project::ProjectSettings settings = {};
    if (!won::project::LoadSettings(source_settings_path, settings))
    {
        std::cout << "Failed to load project settings: " << source_settings_path << "\n";
        return 1;
    }
    if (settings.project_name.empty())
    {
        settings.project_name = target;
    }

    const won::String package_root = won::io::CombinePath(won::io::CombinePath(packages_root, settings.project_name), config);
    if (won::io::GetRelativePath(packages_root, package_root).empty())
    {
        std::cout << "Invalid package directory: " << package_root << "\n";
        return 1;
    }
    std::cout << "Package: " << package_root << "\n";

    if (settings.content_root.empty())
    {
        settings.content_root = "Contents";
    }
    const won::String content_root_path = won::io::NormalizePath(settings.content_root);
    const won::String startup_scene_path = settings.startup_scene.empty() ? "" : won::io::NormalizePath(settings.startup_scene);
    const won::String splash_image_path = won::io::NormalizePath(settings.splash_image);
    const won::String input_action_map_path = won::io::NormalizePath(settings.input_action_map);
    const char* relative_path_names[] = { "content_root", "startup_scene", "splash_image", "input_action_map" };
    const won::String relative_path_values[] = { content_root_path, startup_scene_path, splash_image_path, input_action_map_path };
    for (int path_index = 0; path_index < static_cast<int>(arraysize(relative_path_names)); ++path_index)
    {
        const won::String& path = relative_path_values[path_index];
        if (!path.empty() && (won::io::IsAbsolutePath(path) || path == ".." || path.rfind("../", 0) == 0 || path.find("/../") != won::String::npos))
        {
            std::cout << relative_path_names[path_index] << " must be relative: " << path << "\n";
            return 1;
        }
    }

    // cleanup target package directory
    if (!won::io::RemoveDirectoryRecursive(package_root))
    {
        std::cout << "Failed to clean package directory: " << package_root << "\n";
        return 1;
    }

    // compile shaders to source project directory
    const won::String project_shader_output = won::io::CombinePath(project_root, "CompiledShaders");
    const won::String shader_compiler = won::io::CombinePath(binary_root, "ShaderOfflineCompiler.exe");
    const won::String shader_command = "cmd /c \"\"" + shader_compiler + "\" \"" + project_shader_output + "\"\"";
    if (std::system(shader_command.c_str()) != 0)
    {
        std::cout << "Shader offline compile failed.\n";
        return 1;
    }

    // copy Player
    const won::String target_executable_path = won::io::CombinePath(binary_root, target + ".exe");
    const won::String package_executable_path = won::io::CombinePath(package_root, settings.project_name + ".exe");
    if (!won::io::CopyFileTo(target_executable_path, package_executable_path, true))
    {
        std::cout << "Failed to copy package executable: " << target_executable_path << "\n";
        return 1;
    }

    // copy debug info(Only Debug mode)
    won::Vector<won::String> package_files;
    const won::String pdb_path = won::io::CombinePath(binary_root, target + ".pdb");
    if (copy_debug_info && won::io::IsFile(pdb_path))
    {
        package_files.push_back(pdb_path);
    }
    if (settings.backend_type == won::rendering::RHIBackend::DirectX12)
    {
        const char* dll_names[] = { "dxcompiler.dll", "dxil.dll" };
        for (const char* dll_name : dll_names)
        {
            package_files.push_back(won::io::CombinePath(binary_root, dll_name));
        }
    }
    for (const won::String& package_file : package_files)
    {
        if (!won::io::CopyFileTo(package_file, won::io::CombinePath(package_root, won::io::GetFilename(package_file)), true))
        {
            std::cout << "Failed to copy package file: " << package_file << "\n";
            return 1;
        }
    }

    const won::String content_source = won::io::CombinePath(project_root, settings.content_root);
    const won::String content_target = won::io::CombinePath(package_root, "Contents");
    if (!won::io::CreateDirectories(content_target))
    {
        std::cout << "Failed to create content directory: " << content_target << "\n";
        return 1;
    }

    // copy contents
    won::Vector<won::String> content_paths;
    if (!splash_image_path.empty())
    {
        content_paths.push_back(splash_image_path);
    }
    if (!input_action_map_path.empty() && won::io::IsFile(won::io::CombinePath(content_source, input_action_map_path)))
    {
        content_paths.push_back(input_action_map_path);
    }

    // copy all GameData schema files (Config/*.gamedata) - always included regardless of referenced_assets_only
    {
        const won::String config_source = won::io::CombinePath(content_source, "Config");
        won::Vector<won::io::DirectoryEntry> config_entries;
        if (won::io::EnumerateDirectoryRecursive(config_source, &config_entries))
        {
            for (const won::io::DirectoryEntry& entry : config_entries)
            {
                if (entry.is_file && won::io::GetExtension(entry.path) == won::resource::game_data_schema_extension)
                {
                    content_paths.push_back(won::io::CombinePath("Config", won::io::GetRelativePath(config_source, entry.path)));
                }
            }
        }
    }

    if (referenced_assets_only) // copy only assets referenced by packaged scenes
    {
        // build scene list: packaged_scenes if set, otherwise fall back to startup_scene
        won::Vector<won::String> scenes_to_package = settings.packaged_scenes;
        if (scenes_to_package.empty() && !settings.startup_scene.empty())
            scenes_to_package.push_back(settings.startup_scene);

        // ensure startup_scene is always included
        if (!settings.startup_scene.empty())
        {
            bool found = false;
            for (const won::String& s : scenes_to_package)
            {
                if (s == settings.startup_scene) { found = true; break; }
            }
            if (!found)
                scenes_to_package.insert(scenes_to_package.begin(), settings.startup_scene);
        }

        const char* packaged_content_extensions[] =
        {
            won::resource::scene_file_extension,
            won::resource::mesh_binary_extension,
            won::resource::material_binary_extension,
            won::resource::texture_binary_extension,
            won::resource::lua_script_file_extension,
            won::resource::true_type_font_extension,
            won::resource::open_type_font_extension,
            won::resource::sound_file_extension
        };

        // Some referenced assets are themselves JSON containers that reference further assets:
        // a scene references meshes/textures/materials (and other scenes for transitions),
        // and a material binary (.wonmat) references its textures. Scan them transitively.
        auto is_scannable = [](const won::String& ext)
        {
            return ext == won::resource::scene_file_extension
                || ext == won::resource::material_binary_extension;
        };

        won::Vector<won::String> work_queue = scenes_to_package;
        won::UnorderedSet<won::String> visited;
        for (won::Size head = 0; head < work_queue.size(); ++head)
        {
            won::String current = work_queue[head];
            if (current.rfind("/Contents/", 0) == 0)
                current = current.substr(10);
            if (!visited.insert(current).second)
                continue;
            content_paths.push_back(current);

            if (!is_scannable(won::io::GetExtension(current)))
                continue;

            const won::String asset_path = won::io::CombinePath(content_source, current);
            won::serialize::JsonArchive asset_archive(won::serialize::ArchiveMode::Read);
            if (!asset_archive.LoadFromFile(asset_path))
            {
                std::cout << "Failed to parse asset: " << asset_path << "\n";
                return 1;
            }

            const won::Vector<won::String> asset_strings = asset_archive.GetStringValues();
            for (won::String value : asset_strings)
            {
                if (value.rfind("/Contents/", 0) == 0)
                    value = value.substr(10);
                if (value.empty())
                    continue;
                const won::String ext = won::io::GetExtension(value);
                bool should_copy = false;
                for (const char* packaged_ext : packaged_content_extensions)
                {
                    if (ext == packaged_ext) { should_copy = true; break; }
                }
                if (should_copy)
                    work_queue.push_back(value);
            }
        }
    }
    else // copy all contents
    {
        won::Vector<won::io::DirectoryEntry> content_entries;
        if (!won::io::EnumerateDirectoryRecursive(content_source, &content_entries))
        {
            std::cout << "Failed to iterate content directory: " << content_source << "\n";
            return 1;
        }
        for (const won::io::DirectoryEntry& entry : content_entries)
        {
            if (entry.is_file)
            {
                content_paths.push_back(won::io::GetRelativePath(content_source, entry.path));
            }
        }
    }

    // normalize and validate all content paths before copying
    won::Vector<won::String> normalized_content_paths;
    normalized_content_paths.reserve(content_paths.size());
    for (const won::String& content_path_string : content_paths)
    {
        const won::String content_path = won::io::NormalizePath(content_path_string);
        if (won::io::IsAbsolutePath(content_path) || content_path == ".." || content_path.rfind("../", 0) == 0 || content_path.find("/../") != won::String::npos)
        {
            std::cout << "Content path must be relative: " << content_path << "\n";
            return 1;
        }
        normalized_content_paths.push_back(content_path);
    }

    // pre-copy validation: report all missing files before touching the package directory
    bool any_missing = false;
    for (const won::String& content_path : normalized_content_paths)
    {
        const won::String from = won::io::CombinePath(content_source, content_path);
        if (!won::io::IsFile(from))
        {
            std::cout << "Missing scene dependency: " << content_path << "\n";
            any_missing = true;
        }
    }
    if (any_missing)
    {
        return 1;
    }

    for (const won::String& content_path : normalized_content_paths)
    {
        const won::String from = won::io::CombinePath(content_source, content_path);
        const won::String to = won::io::CombinePath(content_target, content_path);
        if (!won::io::CopyFileTo(from, to, true))
        {
            std::cout << "Failed to copy content file: " << from << "\n";
            return 1;
        }
    }

    const won::String shader_output = won::io::CombinePath(package_root, "CompiledShaders");
    won::Vector<won::io::DirectoryEntry> project_shader_entries;
    if (!won::io::EnumerateDirectoryRecursive(project_shader_output, &project_shader_entries))
    {
        std::cout << "Failed to iterate compiled shaders: " << project_shader_output << "\n";
        return 1;
    }
    for (const won::io::DirectoryEntry& entry : project_shader_entries)
    {
        if (!entry.is_file || won::io::GetExtension(entry.path) != won::resource::shader_binary_extension)
        {
            continue;
        }

        const won::String relative_path = won::io::GetRelativePath(project_shader_output, entry.path);
        if (!won::io::CopyFileTo(entry.path, won::io::CombinePath(shader_output, relative_path), true))
        {
            std::cout << "Failed to copy compiled shader: " << entry.path << "\n";
            return 1;
        }
    }

    const won::String plugins_source = won::io::CombinePath(binary_root, "Plugins");
    for (const won::String& plugin_id : settings.enabled_plugins)
    {
        if (plugin_id.empty() || plugin_id == "." || plugin_id == ".." || plugin_id.find('/') != won::String::npos || plugin_id.find('\\') != won::String::npos)
        {
            std::cout << "Invalid plugin id: " << plugin_id << "\n";
            return 1;
        }

        const won::String plugin_source = won::io::CombinePath(plugins_source, plugin_id);
        const won::String plugin_target = won::io::CombinePath(won::io::CombinePath(package_root, "Plugins"), plugin_id);

        won::Vector<won::io::DirectoryEntry> plugin_entries;
        if (!won::io::EnumerateDirectoryRecursive(plugin_source, &plugin_entries))
        {
            std::cout << "Failed to iterate plugin: " << plugin_source << "\n";
            return 1;
        }
        for (const won::io::DirectoryEntry& entry : plugin_entries)
        {
            if (!entry.is_file)
            {
                continue;
            }
            if (!copy_debug_info && won::io::GetExtension(entry.path) == "pdb")
            {
                continue;
            }

            const won::String relative_path = won::io::GetRelativePath(plugin_source, entry.path);
            const won::String output_path = won::io::CombinePath(plugin_target, relative_path);
            if (!won::io::CopyFileTo(entry.path, output_path, true))
            {
                std::cout << "Failed to copy plugin: " << entry.path << "\n";
                return 1;
            }
        }
    }

    // new setting path
    settings.settings_path.clear();
    settings.project_root.clear();
    settings.content_root = "Contents";
    if (!won::project::SaveSettings(won::io::CombinePath(package_root, won::io::GetFilename(source_settings_path)), settings))
    {
        std::cout << "Failed to write project settings.\n";
        return 1;
    }

    won::Size shader_count = 0;
    won::Vector<won::io::DirectoryEntry> shader_entries;
    if (won::io::EnumerateDirectoryRecursive(shader_output, &shader_entries))
    {
        for (const won::io::DirectoryEntry& entry : shader_entries)
        {
            if (entry.is_file && won::io::GetExtension(entry.path) == "woncso")
            {
                ++shader_count;
            }
        }
    }
    if (shader_count == 0)
    {
        std::cout << "Compiled shader binaries not found: " << shader_output << "\n";
        return 1;
    }

    if (generate_manifest)
    {
        won::serialize::JsonArchive manifest(won::serialize::ArchiveMode::Write);
        manifest.BeginObject();
        manifest.Field("project_name", settings.project_name);
        manifest.Field("build_config", config);
        manifest.BeginArray("assets");
        for (const won::String& content_path : normalized_content_paths)
        {
            won::String path_copy = content_path;
            manifest.Item(path_copy);
        }
        manifest.EndArray();
        manifest.EndObject();

        const won::String manifest_path = won::io::CombinePath(package_root, "cook_manifest.json");
        if (!manifest.SaveToFile(manifest_path))
        {
            std::cout << "Failed to write cook_manifest.json\n";
            return 1;
        }
        std::cout << "Manifest: " << manifest_path << "\n";
    }

    std::cout << "Package completed: " << package_root << "\n";
    return 0;
}
