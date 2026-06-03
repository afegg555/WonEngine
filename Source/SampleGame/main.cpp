#include "Application.h"

int main()
{
    won::ApplicationDesc app_desc = {};
    app_desc.window.title = "Won Engine Sample Game";
    app_desc.window.fullscreen = false;
    app_desc.window.use_title_bar = true;
    app_desc.backend_type = won::rendering::RHIBackend::DirectX12;
    app_desc.jobsystem_thread_count = ~0u;
    app_desc.device_preference = won::rendering::RHIDevicePreference::Default;

    won::Application app;
    app.Initialize(app_desc);

    while (app.IsRunning())
    {
        app.Run();
    }

    return 0;
}
