#include "Editor.h"
#include "FileSystem.h"
#include "ProjectSettings.h"

using namespace won;
using namespace won::editor;

namespace
{
	constexpr const char* editor_project_settings_file_name = "EditorProjectSettings.json";
}

int main()
{
	ApplicationDesc app_desc = {};
	String project_settings_path = io::CombinePath(io::GetExecutableDirectory(), editor_project_settings_file_name);
	app_desc.project_settings.project_name = "Editor";
	app_desc.project_settings.window_title = "Won Engine";
#ifdef CONTENTS_ROOT_DIR
	String development_content_root = CONTENTS_ROOT_DIR;
	if (io::IsDirectory(development_content_root))
	{
		app_desc.project_settings.project_root = io::CombinePath(development_content_root, "..");
	}
#endif
#ifdef EDITOR_USE_CUSTOM_TITLEBAR
	app_desc.project_settings.window_use_title_bar = false;
#else
	app_desc.project_settings.window_use_title_bar = true;
#endif
	project::LoadSettings(project_settings_path, app_desc.project_settings);

	EditorApplication app;
	app.Initialize(app_desc);

	while (app.IsRunning())
	{
		app.Run();
	}

	app.Shutdown();

	return 0;
}
