#include "Application.h"
#include "PluginManager.h"
#include "Entity.h"

namespace won::editor
{
	class EditorApplication : public Application
	{
	public:
		void Initialize(const ApplicationDesc& desc) override;
		void Shutdown() override;
		void Update(float dt) override;

	protected:
		void OnWindowResized(int width, int height) override;
		void RenderUI() override;

	private:
		void LoadDefaultPlugins();
		void InitImGui();
		void LoadSampleScene();
		void UpdateEntityList();

	private:
		std::shared_ptr<RHIPipeline> imgui_pso;
		std::shared_ptr<RHIResource> imgui_font;
		RHISubresourceHandle imgui_font_subresource;
		std::shared_ptr<RHISampler> imgui_sampler;

		std::vector<ecs::Entity> sorted_entities;

		ecs::Scene scene;
		ecs::Entity camera_entity;
		ecs::Entity picked_entity;

		plugin::PluginManager plugin_manager;
	};
}
