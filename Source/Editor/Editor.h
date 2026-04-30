#include "Application.h"
#include "PluginManager.h"
#include "Entity.h"
#include "Mesh.h"

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
		void UpdateEditorPrimitiveMesh();

	private:
		struct ViewportDebugSettings
		{
			bool show_ddgi_overlay = false;
			bool show_ddgi_volume = true;
			bool show_ddgi_probes = true;
			bool show_ddgi_text = true;
			bool show_bvh_debug = false;
			bool show_cpu_bvh_nodes = true;
			bool show_gpu_bvh_nodes = true;
			int ddgi_max_probe_draw_count = 256;
		};

		std::shared_ptr<RHIPipeline> imgui_pso;
		std::shared_ptr<RHIResource> imgui_font;
		RHISubresourceHandle imgui_font_subresource;
		std::shared_ptr<RHISampler> imgui_sampler;

		std::vector<ecs::Entity> sorted_entities;

		ecs::Scene scene;
		ecs::Entity camera_entity;
		ecs::Entity editor_primitive_entity = ecs::INVALID_ENTITY;
		std::shared_ptr<resource::Mesh> editor_primitive_mesh;
		std::vector<std::shared_ptr<RHIResource>> deferred_primitive_removal_buffers;
		ecs::Entity picked_entity = ecs::INVALID_ENTITY;
		ViewportDebugSettings viewport_debug_settings = {};

		plugin::PluginManager plugin_manager;
	};
}
