#pragma once
#include "Types.h"
#include "Renderer.h"
#include "Timer.h"
#include "Window.h"
#include "View.h"
#include "RHIDevice.h"

#include <memory>

#pragma warning(push)
#pragma warning(disable: 4251)

namespace won
{
    struct ApplicationDesc
    {
        platform::WindowDesc window = {};
        rendering::RHIBackend backend_type = rendering::RHIBackend::DirectX12;
        uint32 jobsystem_thread_count = ~0u;
    };

    class WONENGINE_API Application
    {
    public:
        bool IsRunning() const;
        void Run();

        virtual void Initialize(const ApplicationDesc& desc);
        virtual void Shutdown();
        virtual void Update(float dt);
        virtual void Render();

    protected:
        void WaitIdle();
        virtual void RenderScene();
        virtual void RenderUI();
        virtual void OnWindowResized(int width, int height);

        void ProcessWindowResize();

        bool is_running = false;
        std::shared_ptr<rendering::RHIDevice> device;
        std::shared_ptr<platform::Window> window;
        std::shared_ptr<rendering::Renderer> renderer;
        rendering::View main_view;
        utils::Timer frame_timer;
        bool is_first_frame = true;
    };
}

#pragma warning(pop)
