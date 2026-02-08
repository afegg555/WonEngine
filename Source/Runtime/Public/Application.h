#pragma once
#include "Types.h"
#include "Renderer.h"
#include "Swapchain.h"
#include "View.h"
#include "RHIDevice.h"

#include <memory>

#pragma warning(push)
#pragma warning(disable: 4251)

namespace won
{
    struct ApplicationDesc
    {
        platform::SwapchainDesc swapchain = {};
        rendering::RendererType renderer_type = rendering::RendererType::Forward;
        rendering::RHIBackend backend_type = rendering::RHIBackend::DirectX12;
        uint32 jobsystem_thread_count = ~0;
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

    private:
        bool is_running = false;
        std::shared_ptr<platform::Swapchain> swapchain;
        std::shared_ptr<rendering::Renderer> renderer;
        rendering::View main_view;
    };
}

#pragma warning(pop)