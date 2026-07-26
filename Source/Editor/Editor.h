#include "Application.h"
#include "EditorSettings.h"
#include "FileSystem.h"
#include "GameData.h"
#include "JobSystem.h"
#include "Plugin.h"
#include "Entity.h"
#include "Mesh.h"
#include "MaterialComponent.h"

using namespace won::rendering;

#define EDITOR_USE_CUSTOM_TITLEBAR

namespace won::ecs
{
	struct CameraComponent;
	struct TransformComponent;
}

namespace won::editor
{
	class EditorApplication : public Application
	{
	public:
		void Initialize(const ApplicationDesc& desc) override;
		void Initialize(const ApplicationDesc& desc, const project::ProjectSettings& loaded_project_settings_in);
		void Shutdown() override;
		void Update(float dt) override;

	protected:
		void OnWindowResized(int width, int height) override;
		void RenderUI() override;

	private:
		struct EditorAssetImporter
		{
			struct ImportTask
			{
				String path; // absolute path of asset
				std::atomic_bool finished{ false };
				std::atomic_bool failed{ false };
				jobsystem::Context context;
				uint64 id = 0;
				bool add_to_scene = false;
				uint64 bg_task_id = 0;
			};

			std::vector<std::shared_ptr<ImportTask>> tasks;
		};

		struct BackgroundTask
		{
			enum class State { Running, Done, Failed };
			uint64 id = 0;
			String name;
			State state = State::Running;
			float progress = -1.0f; // -1 = indeterminate
			double finished_time = 0.0;
		};

		struct BackgroundTaskState
		{
			std::vector<BackgroundTask> tasks;
			uint64 next_id = 1;
		};

		void LoadPlugins();
		void RegisterPluginExtensions(const std::shared_ptr<plugin::Plugin>& plugin, ecs::Scene& scene);
		void SetPluginEnabled(Size plugin_index, bool enabled);
		void InitImGui();
		void InitEditorGrid();
		void DrawEditorGrid();
		void CreateEditorCamera();
		void CreateStartupScene();
		bool NewProject(const String& path);
		bool LoadProject(const String& path);
		bool SaveProject();
		bool SaveScene(const String& path);
		bool SavePrefab(const String& path, ecs::Entity root);
		void LoadScene(const String& path);
		void InstantiatePrefab(const String& path);
		void EnterPlay();
		void ExitPlay();
		void DrawProjectSettingsWindow(bool* open);
		void RebindSceneResources();
		void UpdateEntityList();
		uint64 StartAssetImport(const String& path, bool add_to_scene);
		bool CommitAssetImportResult(EditorAssetImporter::ImportTask& task);
		uint64 AddBackgroundTask(const String& name);
		void FinishBackgroundTask(uint64 id, bool failed);
		void DrawBackgroundTaskStatus();
		void LoadEditorSettings();
		void SaveEditorSettings();

	private:
		enum class ContentAssetType
		{
			All,
			Texture,
			Material,
			Mesh,
			Scene,
			Prefab,
			Shader,
			Font,
			Script,
			Unknown,
		};

		struct ContentBrowserAsset
		{
			uint64 id = 0;
			String name;
			String virtual_path;
			String disk_path;
			ContentAssetType type = ContentAssetType::Unknown;
			String reimport_source_path; // resolved source to reimport from (imported binaries)
			bool needs_reimport = false; // source changed since last import
			bool has_broken_reference = false;
			String broken_reason;
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
			bool pending_import_add_to_scene = false;
		};

		void RebuildContentBrowser();
		void DrawContentsBrowser();
		void DrawContentFolderNode(const String& virtual_path, const String& name);
		void DrawContentAssetTile(const ContentBrowserAsset& asset, float tile_size);

		struct EditorPluginInfo
		{
			plugin::PluginInfo info;
			std::shared_ptr<plugin::Plugin> plugin;
			bool enabled = false;
			bool registered = false;
		};

		struct ViewportDebugSettings
		{
			bool show_grid = false;
			bool show_colliders = true;
			bool use_wireframe = false;
			bool show_ddgi_overlay = false;
			bool show_bvh_debug = false;
		};

		struct EditorViewport
		{
			struct CameraController
			{
				enum class InteractionMode
				{
					None,
					PanMove,
					Rotate,
					Orbit,
				};

				bool pressed = false;
				float2 prev_mouse_pos = {};
				InteractionMode active_interaction = InteractionMode::None;
				float yaw = 0.0f;
				float pitch = 0.0f;
				float orbit_distance = 0.0f;
				float move_speed = 5.0f;
				float rotate_speed = 1.0f;
				float orbit_speed = 1.0f;
				float3 focus_point = { 0.0f, 0.0f, 0.0f };

				void Update(const ecs::CameraComponent& camera, ecs::TransformComponent& transform, float dt, const float2& viewport_mouse_pos, const float2& viewport_size, bool can_begin_interaction);
				void BeginInteraction(InteractionMode mode, const ecs::TransformComponent& transform, const float2& viewport_mouse_pos);
				void UpdateInteraction(ecs::TransformComponent& transform, const float2& viewport_mouse_pos, const float2& viewport_size);
				void EndInteraction();
			};

			struct DeferredResRemoval
			{
				uint32 frames_left = 0;
				std::vector<std::shared_ptr<resource::Mesh>> meshes;
				std::vector<std::shared_ptr<RHIResource>> resources;
			};

			rendering::View* view = nullptr;
			CameraController camera_controller;
			std::vector<DeferredResRemoval> deferred_res_removals;
			ecs::Entity picked_entity = ecs::INVALID_ENTITY;
			bool input_enabled = false;
			ViewportDebugSettings debug_settings = {};
		};

		std::shared_ptr<RHIPipeline> imgui_pso;
		std::shared_ptr<RHIResource> imgui_font;
		RHISubresourceHandle imgui_font_subresource;
		std::shared_ptr<RHISampler> imgui_sampler;
		std::shared_ptr<RHIPipeline> editor_grid_pso;

		std::vector<ecs::Entity> sorted_entities;
		std::vector<EditorPluginInfo> plugins;

		struct GameDataEditorState
		{
			game::GameData game_data;
			String loaded_schema_path;
			bool dirty = false;
			char new_key[256] = {};
			int new_type_index = 0;
			char new_default[256] = {};
			char new_schema_filename[256] = {};
		};

		project::ProjectSettings loaded_project_settings;
		String current_scene_path;
		bool is_playing = false;
		bool is_paused = false;
		bool request_play = false;
		bool request_stop = false;
		bool request_step = false;
		ecs::Scene* edit_scene = nullptr;
		ecs::Scene* play_scene = nullptr;
		ecs::Entity edit_camera_entity = ecs::INVALID_ENTITY;
		bool show_project_settings_window = false;
		GameDataEditorState game_data_editor = {};
		EditorViewport editor_viewport;
		EditorAssetImporter asset_importer;
		BackgroundTaskState background_tasks;
		ContentBrowserState content_browser = {};
		EditorSettings editor_settings;
		std::unique_ptr<io::DirectoryWatcher> contents_watcher;
		float contents_watcher_poll_timer = 0.0f;
		float editor_camera_speed = 5.0f;
	};
}
