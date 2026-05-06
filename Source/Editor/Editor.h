#include "Application.h"
#include "PluginManager.h"
#include "Entity.h"
#include "Mesh.h"

namespace won::plugin
{
	struct AssetImportTask;
}

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
		void InitEditorGrid();
		void DrawEditorGrid();
		void LoadSampleScene();
		void UpdateEntityList();
		void UpdateEditorPrimitiveMesh();

	private:
		enum class ContentAssetType
		{
			All,
			Texture,
			Material,
			Mesh,
			Scene,
			Shader,
			Font,
			Unknown,
		};

		struct ContentBrowserAsset
		{
			uint64 id = 0;
			String name;
			String virtual_path;
			String disk_path;
			ContentAssetType type = ContentAssetType::Unknown;
		};

		struct ContentBrowserState
		{
			bool initialized = false;
			String current_folder = "/Contents";
			char search[256] = {};
			ContentAssetType type_filter = ContentAssetType::All;
			float tile_size = 72.0f;
			std::vector<ContentBrowserAsset> assets;
			std::vector<String> folders;
			bool open_import_confirm = false;
			String pending_import_name;
			String pending_import_virtual_path;
			String pending_import_disk_path;
			ContentAssetType pending_import_type = ContentAssetType::Unknown;
		};

		void RebuildContentBrowser();
		void DrawContentsBrowser();
		void DrawContentFolderNode(const String& virtual_path, const String& name);
		void DrawContentAssetTile(const ContentBrowserAsset& asset, float tile_size);

		struct ViewportDebugSettings
		{
			bool show_grid = false;
			bool show_ddgi_overlay = false;
			bool show_ddgi_volume = true;
			bool show_ddgi_probes = true;
			bool show_ddgi_text = true;
			bool show_bvh_debug = false;
			bool show_cpu_bvh_nodes = true;
			bool show_gpu_bvh_nodes = true;
			bool use_wireframe = false;
			int ddgi_max_probe_draw_count = 4096;
		};

		struct DeferredEntityRemovalResources
		{
			uint32 frames_left = 0;
			std::vector<std::shared_ptr<resource::Mesh>> meshes;
			std::vector<std::shared_ptr<RHIResource>> resources;
		};

		std::shared_ptr<RHIPipeline> imgui_pso;
		std::shared_ptr<RHIResource> imgui_font;
		RHISubresourceHandle imgui_font_subresource;
		std::shared_ptr<RHISampler> imgui_sampler;
		std::shared_ptr<RHIPipeline> editor_grid_pso;

		std::vector<ecs::Entity> sorted_entities;

		ecs::Scene scene;
		ecs::Entity camera_entity;
		ecs::Entity editor_primitive_entity = ecs::INVALID_ENTITY;
		std::shared_ptr<resource::Mesh> editor_primitive_mesh;
		std::vector<std::shared_ptr<RHIResource>> deferred_primitive_removal_buffers;
		std::vector<DeferredEntityRemovalResources> deferred_entity_removal_resources;
		std::vector<std::shared_ptr<plugin::AssetImportTask>> asset_import_tasks;
		ecs::Entity picked_entity = ecs::INVALID_ENTITY;
		bool viewport_input_enabled = false;
		ViewportDebugSettings viewport_debug_settings = {};
		ContentBrowserState content_browser = {};

		std::shared_ptr<plugin::PluginManager> plugin_manager;
	};
}
