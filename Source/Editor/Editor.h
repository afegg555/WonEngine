#include "Application.h"
#include "PluginManager.h"

namespace won::editor
{
	class EditorApplication : public Application
	{
	public:
		void Initialize(const ApplicationDesc& desc) override;
		void Shutdown() override;
		void Update(float dt) override;

	private:
		void LoadDefaultPlugins();
		void OnWindowResized(int width, int height) override;
		void RenderUI() override;
		void ImGui_Impl_CreateDeviceObjects();

		void LoadSampleScene();

	private:
		std::shared_ptr<RHIPipeline> imgui_pso;
		std::shared_ptr<RHIResource> imgui_font;
		RHISubresourceHandle imgui_font_subresource;
		std::shared_ptr<RHISampler> imgui_sampler;

		float2 main_viewport_pos;
		float2 main_viewport_size;

		ecs::Scene scene;
		ecs::Entity camera_entity;
		ecs::Entity image_entity;

		plugin::PluginManager plugin_manager;
	};
}
