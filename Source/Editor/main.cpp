#include "Editor.h"

using namespace won;
using namespace won::editor;
int main()
{
	ApplicationDesc app_desc;
	app_desc.window.title = "Won Engine";
	app_desc.window.fullscreen = false;
	app_desc.backend_type = rendering::RHIBackend::DirectX12;
	app_desc.jobsystem_thread_count = ~0;

	EditorApplication app;
	app.Initialize(app_desc);

	do
	{
		app.Run();
	} while (app.IsRunning());

	return 0;
}
