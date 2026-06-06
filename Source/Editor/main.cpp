#include "Editor.h"
#include "FileSystem.h"
#include "ProjectSettings.h"

#include <cstdio>

using namespace won;
using namespace won::editor;

int main(int argc, char** argv)
{
	ApplicationDesc app_desc = {};
	const String editor_project_settings_path = io::CombinePath(io::GetExecutableDirectory(), "Editor.wonproj");
	app_desc.project_settings.settings_path = io::NormalizePath(editor_project_settings_path);
	app_desc.project_settings.project_root = io::NormalizePath(io::GetExecutableDirectory());
	app_desc.project_settings.project_name = "WonEditor";
	app_desc.project_settings.content_root = io::NormalizePath(String(CONTENTS_ROOT_DIR));
	app_desc.project_settings.window_title = "Won Engine";
#ifdef EDITOR_USE_CUSTOM_TITLEBAR
	app_desc.project_settings.window_use_title_bar = false;
#else
	app_desc.project_settings.window_use_title_bar = true;
#endif
	if (io::IsFile(editor_project_settings_path) && !project::LoadSettings(editor_project_settings_path, app_desc.project_settings))
	{
		std::fprintf(stderr, "Failed to load editor project: %s\n", editor_project_settings_path.c_str());
		return 1;
	}

	String project_settings_path;
	if (argc > 1 && argv && argv[1] && argv[1][0] != '\0')
	{
		project_settings_path = io::GetAbsolutePath(argv[1]);
	}
	else
	{
		io::FileDialogDesc desc = {};
		desc.title = "Open Won Project";
		const String projects_directory = io::NormalizePath(String(PROJECTS_ROOT_DIR));
		desc.initial_directory = io::IsDirectory(projects_directory) ? projects_directory : io::GetExecutableDirectory();
		desc.default_extension = project::project_file_extension;
		desc.filter_name = "Won Project";
		desc.filter_pattern = "*.wonproj";
		if (!io::OpenFileDialog(project_settings_path, desc))
		{
			return 1;
		}
	}

	if (!io::IsFile(project_settings_path) && io::GetExtension(project_settings_path) != project::project_file_extension)
	{
		const String project_path = project_settings_path;
		project_settings_path = io::CombinePath(project_path, io::ReplaceExtension(io::GetFilename(project_path), project::project_file_extension));
		if (!io::IsFile(project_settings_path))
		{
			project_settings_path = io::CombinePath(project_path, project::default_project_file_name);
		}
	}

	project_settings_path = io::NormalizePath(project_settings_path);
	project::ProjectSettings loaded_project_settings = {};
	if (!io::IsFile(project_settings_path) || !project::LoadSettings(project_settings_path, loaded_project_settings))
	{
		std::fprintf(stderr, "Failed to load project: %s\n", project_settings_path.c_str());
		return 1;
	}

	EditorApplication app;
	app.Initialize(app_desc, loaded_project_settings);

	while (app.IsRunning())
	{
		app.Run();
	}

	app.Shutdown();

	return 0;
}
