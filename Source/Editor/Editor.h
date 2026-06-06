#include "Application.h"
#include "EditorSettings.h"
#include "FileSystem.h"
#include "JobSystem.h"
#include "Plugin.h"
#include "Entity.h"
#include "Mesh.h"
#include "MaterialComponent.h"

#define EDITOR_USE_CUSTOM_TITLEBAR

namespace won::ecs
{
	struct CameraComponent;
	struct TransformComponent;
}

namespace won::resource
{
	struct AnimationClip;
	struct Image;
	struct Skeleton;
}

namespace won::plugin::function
{
	struct Desc;
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
			struct Functions
			{
				std::shared_ptr<plugin::Plugin> plugin;
				const plugin::function::Desc* import = nullptr;
				const plugin::function::Desc* get_result_info = nullptr;
				const plugin::function::Desc* get_stream_info = nullptr;
				const plugin::function::Desc* copy_stream = nullptr;
				const plugin::function::Desc* get_struct_field_count = nullptr;
				const plugin::function::Desc* get_struct_field_info = nullptr;
				const plugin::function::Desc* get_material_info = nullptr;
				const plugin::function::Desc* get_material_texture_count = nullptr;
				const plugin::function::Desc* get_material_texture = nullptr;
				const plugin::function::Desc* get_embedded_texture_info = nullptr;
				const plugin::function::Desc* copy_embedded_texture = nullptr;
				const plugin::function::Desc* get_bone_name = nullptr;
				const plugin::function::Desc* get_animation_clip_name = nullptr;
				const plugin::function::Desc* release_result = nullptr;

				bool IsValid() const
				{
					return plugin && import && get_result_info && get_stream_info && copy_stream && get_struct_field_count && get_struct_field_info &&
						get_material_info && get_material_texture_count && get_material_texture && get_embedded_texture_info && copy_embedded_texture &&
						get_bone_name && get_animation_clip_name && release_result;
				}
			};

			struct TextureRequest
			{
				uint32 material_index = 0;
				uint32 texture_slot = 0;
				uint32 source_type = 0;
				String source_path;
				uint32 embedded_width = 0;
				uint32 embedded_height = 0;
				bool embedded_compressed = false;
				Vector<uint8> embedded_bytes;
			};

			struct PreparedAsset
			{
				String name;
				std::shared_ptr<resource::Mesh> mesh;
				std::shared_ptr<resource::Skeleton> skeleton;
				Vector<std::shared_ptr<resource::AnimationClip>> animation_clips;
				Vector<ecs::MaterialSlot> material_slots;
				Vector<TextureRequest> texture_requests;
			};

			struct ImportTask
			{
				String path; // absolute path of asset
				std::atomic_bool finished{ false };
				std::atomic_bool failed{ false };
				std::atomic<uint64> result_handle{ 0 };
				jobsystem::Context context;
				PreparedAsset prepared;
				uint64 id = 0;
			};

			struct TextureLoadTask
			{
				ecs::Entity entity = ecs::INVALID_ENTITY;
				uint32 material_index = 0;
				uint32 texture_slot = 0;
				uint32 source_type = 0; // file or embedded
				String source_path;
				String asset_id;
				String binary_path;
				uint32 embedded_width = 0;
				uint32 embedded_height = 0;
				bool embedded_compressed = false;
				Vector<uint8> embedded_bytes;
				
				std::shared_ptr<resource::Image> image;
				std::atomic_bool finished{ false };
				std::atomic_bool failed{ false };
				jobsystem::Context context;
			};

			Functions functions;
			uint64 sample_script_task_id = 0;
			std::vector<std::shared_ptr<ImportTask>> tasks;
			std::vector<std::shared_ptr<TextureLoadTask>> texture_tasks;

			bool IsValid() const
			{
				return functions.IsValid();
			}
		};

		void LoadPlugins();
		void RegisterPluginExtensions(const std::shared_ptr<plugin::Plugin>& plugin);
		void SetPluginEnabled(Size plugin_index, bool enabled);
		void InitImGui();
		void InitEditorGrid();
		void DrawEditorGrid();
		void CreateEditorCamera();
		void CreateStartupScene();
		bool SaveScene(const String& path);
		void LoadScene(const String& path);
		void RebindSceneResources();
		void UpdateEntityList();
		void UpdateDebugPrimitiveMesh();
		uint64 StartAssetImport(const String& path);
		bool CommitAssetImportResult(EditorAssetImporter::ImportTask& task);
		void ReleaseAssetImportResult(uint64 result_handle);
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
			ecs::Entity debug_primitive_entity = ecs::INVALID_ENTITY;
			std::shared_ptr<resource::Mesh> debug_primitive_mesh;
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

		project::ProjectSettings loaded_project_settings;
		ecs::Scene loaded_scene;
		String current_scene_path;
		EditorViewport editor_viewport;
		EditorAssetImporter asset_importer;
		ContentBrowserState content_browser = {};
		EditorSettings editor_settings;
		std::unique_ptr<io::DirectoryWatcher> contents_watcher;
		float contents_watcher_poll_timer = 0.0f;
		float editor_camera_speed = 5.0f;
	};
}
