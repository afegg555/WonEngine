#pragma once
#include "RuntimeExport.h"
#include "RHIDevice.h"
#include "View.h"
#include "Window.h"
#include "ShaderLibrary.h"
#include "RHIResource.h"

#include <memory>

namespace won::rendering
{
    enum class RendererType
    {
        Forward
    };

    struct RendererDesc
    {
        std::shared_ptr<RHIDevice> device;
        RendererType type = RendererType::Forward;
    };

    constexpr RHIFormat RENDERTARGET_BUFFER_FORMAT = RHIFormat::R8G8B8A8Unorm;
    constexpr RHIFormat DEPTH_BUFFER_FORMAT = RHIFormat::D32Float;

    class WONENGINE_API Renderer
    {
    public:
        virtual ~Renderer() = default;

        virtual void Initialize(const RendererDesc& desc, std::shared_ptr<resource::ShaderLibrary> shader_lib) = 0;
        virtual void BeginFrame(platform::Window& window) = 0;
        virtual void Render(const View& view) = 0;
        virtual void EndFrame() = 0;
        virtual void Shutdown() = 0;

    protected:
        std::shared_ptr<resource::ShaderLibrary> shader_library;
    };

    WONENGINE_API std::shared_ptr<Renderer> CreateRenderer(const RendererDesc& desc);
    WONENGINE_API void ReloadShaderLibrary(std::shared_ptr<RHIDevice> device);
}
