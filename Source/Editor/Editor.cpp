#include "Editor.h"
#include "Input.h"
#include "ShaderCompiler.h"
#include "RHIResource.h"
#include "RHIShader.h"
#include "RHIPipeline.h"
#include "RenderingUtils.h"
#include "Animation.h"
#include "ShaderCompiler.h"
#include "FileSystem.h"
#include "Image.h"
#include "ResourceAsset.h"
#include "TerrainGenerator.h"
#include "StringUtils.h"
#include "Backlog.h"
#include "Profiler.h"
#include "SceneComponents.h"
#include "JobSystem.h"
#include "EventHandler.h"
#include "Reflection.h"
#include "BuiltinTypeMeta.h"
#include "SceneSerializer.h"
#include "SplashWindow.h"

#include "CustomComponentExtension.h"
#include "CustomFunctionExtension.h"
#include "CustomSystemExtension.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui-docking/imgui.h"
#include "imgui-docking/imgui_internal.h"
#ifdef _WIN32
#include "imgui-docking/imgui_impl_win32.h"
#include <shellapi.h>
#endif
#include "IconsMaterialDesign.h"
#include "Themes.h"

#include <filesystem>

#define DEFAULTBUTTONWIDTH 200

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace won::editor
{
	using namespace resource;
	using namespace rendering;
	using namespace plugin;
	using namespace ecs;
	namespace function = won::plugin::function;

	static RHIShader imgui_vs;
	static RHIShader imgui_ps;
	static RHIShader editor_grid_vs;
	static RHIShader editor_grid_ps;
	static String contents_root_dir;
	static String editor_contents_root_dir;
	namespace
	{


		constexpr const char* generated_asset_directory = "Generated";
		constexpr const char* scene_directory_name = "Scenes";
		constexpr const char* default_scene_file_name = "NewScene";

		namespace editor_text
		{
			constexpr const char* main_window = "Main";
			constexpr const char* file_menu = "File";
			constexpr const char* build_menu = "Build";
			constexpr const char* window_menu = "Window";
			constexpr const char* plugins_menu = "Plugins";
			constexpr const char* package_project = "Package Project";
			constexpr const char* package_project_started = "Packaging project: ";
			constexpr const char* package_project_completed = "Package completed.";
			constexpr const char* package_project_failed = "Package failed.";
			constexpr const char* package_project_missing_settings = "Project settings path is empty.";
			constexpr const char* package_tool_not_found = "PackageTool not found: ";
			constexpr const char* splash_starting_renderer = "Starting renderer...";
			constexpr const char* splash_loading_editor_settings = "Loading editor settings...";
			constexpr const char* splash_loading_plugins = "Loading plugins...";
			constexpr const char* splash_loading_startup_scene = "Loading startup scene...";
			constexpr const char* new_project = "New Project";
			constexpr const char* load_project = "Load Project";
			constexpr const char* save_project = "Save Project";
			constexpr const char* project_settings = "Project Settings";
			constexpr const char* no_project = "No Project";
			constexpr const char* new_scene = "New Scene";
			constexpr const char* save_scene = "Save Scene";
			constexpr const char* save_scene_as = "Save Scene As...";
			constexpr const char* load_scene = "Load Scene";
			constexpr const char* save_current_scene_popup = "Save Current Scene?###SaveCurrentScenePopup";
			constexpr const char* save_current_scene_message = "Save current scene?";
			constexpr const char* unsaved_scene = "Unsaved Scene";
			constexpr const char* path = "Path";
			constexpr const char* won_project_file = "Won Project";
			constexpr const char* won_scene_file = "Won Scene";
			constexpr const char* value = "Value";
			constexpr const char* save = "Save";
			constexpr const char* save_as = "Save As";
			constexpr const char* load = "Load";
			constexpr const char* dont_save = "Don't Save";
			constexpr const char* cancel = "Cancel";
			constexpr const char* reset_layout = "Reset Layout";
			constexpr const char* no_plugins_found = "No plugins found";
			constexpr const char* unknown = "Unknown";
			constexpr const char* plugin_loaded = "Loaded";
			constexpr const char* plugin_not_loaded = "Not loaded";
			constexpr const char* viewport_window = "Viewport";
			constexpr const char* options = "Options";
			constexpr const char* options_popup = "OptionsPopup";
			constexpr const char* wireframe = "WireFrame";
			constexpr const char* anti_aliasing = "Anti-Aliasing";
			constexpr const char* tonemap_mode = "Tonemap Mode";
			constexpr const char* vsync = "VSync";
			constexpr const char* reset_editor_camera = "Reset Editor Camera";
			constexpr const char* editor_grid = "Editor Grid";
			constexpr const char* collider_3d = "Collider3D";
			constexpr const char* bvh_debug = "BVH Debug";
			constexpr const char* cpu_bvh_nodes = "CPU BVH Nodes";
			constexpr const char* gpu_bvh_nodes = "GPU BVH Nodes";
			constexpr const char* renderer_stats = "Renderer Stats";
			constexpr const char* ddgi_debug_overlay = "DDGI Debug Overlay";
			constexpr const char* ddgi_volume = "DDGI Volume";
			constexpr const char* ddgi_probes = "DDGI Probes";
			constexpr const char* ddgi_text = "DDGI Text";
			constexpr const char* ddgi_max_probe_draw = "DDGI Max Probe Draw";
			constexpr const char* close = "Close";
			constexpr const char* entity_list_window = "Entity List";
			constexpr const char* delete_entity = "Delete Entity";
			constexpr const char* delete_entity_confirm_popup = "Delete Entity Confirm";
			constexpr const char* delete_button = "Delete";
			constexpr const char* delete_confirm_format = "Delete %s?";
			constexpr const char* delete_entity_children_warning = "Children and components will also be removed.";
			constexpr const char* entity_label = "Entity ";
			constexpr const char* importing_asset = "Importing asset";
			constexpr const char* importing_assets = "Importing assets (";
			constexpr const char* inspector_window = "Inspector";
			constexpr const char* log_window = "Log";
			constexpr const char* copy_to_clipboard = "Copy to Clipboard";
			constexpr const char* clear = "Clear";
			constexpr const char* contents_browser_window = "Contents Browser";
			constexpr const char* profiler_window = "Profiler";
			constexpr const char* enable_profiler = "Enable Profiler";
			constexpr const char* profiler_starting = "Profiler starting...";
			constexpr const char* profiler_warning = "Profiler Turned On! Performance may be reduced!";
			constexpr const char* import_to_scene = "Import to Scene";
			constexpr const char* copy_disk_path = "Copy Disk Path";
			constexpr const char* copy_virtual_path = "Copy Virtual Path";
			constexpr const char* refresh = "Refresh";
			constexpr const char* folder = "Folder";
			constexpr const char* folder_empty = "Folder is empty.";
			constexpr const char* import_content_asset_popup = "Import Content Asset";
			constexpr const char* import_content_asset_message = "Import this asset to Scene?";
			constexpr const char* import = "Import";
			constexpr const char* asset_texture = "Texture";
			constexpr const char* asset_material = "Material";
			constexpr const char* asset_mesh = "Mesh";
			constexpr const char* asset_scene = "Scene";
			constexpr const char* asset_shader = "Shader";
			constexpr const char* asset_font = "Font";
			constexpr const char* asset_script = "Script";
			constexpr const char* asset = "Asset";
			constexpr const char* all_types = "All Types";
			constexpr const char* contents = "Contents";
			constexpr const char* search_hint = ICON_MD_SEARCH " Search";
			constexpr const char* size_format = "Size %.0f";
			constexpr const char* path_label = "Path:";
			constexpr const char* remove_component_popup = "Remove Component";
			constexpr const char* remove = "Remove";
			constexpr const char* remove_tooltip_format = "Remove %s";
			constexpr const char* remove_confirm_format = "Remove %s?";
			constexpr const char* action_cannot_be_undone = "This action cannot be undone.";
			constexpr const char* unsupported_format = "%s: unsupported";
			constexpr const char* assigned = "Assigned";
			constexpr const char* none = "None";
			constexpr const char* no_components_available = "No components available";
			constexpr const char* add_component = "Add Component";
			constexpr const char* add_component_popup = "AddComponentPopup";
			constexpr const char* update_scene_gpubvh = "Update Scene GPUBVH";
			constexpr const char* regenerate_terrain = "Regenerate Terrain";
			constexpr const char* add_slot = "Add Slot";
			constexpr const char* remove_slot = "Remove Slot";
			constexpr const char* reload = "Reload";
			constexpr const char* up = "Up";
			constexpr const char* down = "Down";
			constexpr const char* add_script = "Add Script";
			constexpr const char* active = "Active";
			constexpr const char* dynamic = "Dynamic";
			constexpr const char* cast_shadow = "Cast Shadow";
			constexpr const char* orthographic = "Orthographic";
			constexpr const char* billboard = "Billboard";
			constexpr const char* loop = "Loop";
			constexpr const char* enabled = "Enabled";
			constexpr const char* script_enabled = "Script Enabled";
			constexpr const char* double_sided = "Double Sided";
			constexpr const char* use_vertex_colors = "Use Vertex Colors";
			constexpr const char* receive_shadow = "Receive Shadow";
			constexpr const char* duration_zero = "Duration: 0.000s";
			constexpr const char* no_animation_clips = "No animation clips";
			constexpr const char* name_component = "NameComponent";
			constexpr const char* transform_component = "TransformComponent";
			constexpr const char* hierarchy_component = "HierarchyComponent";
			constexpr const char* light_component = "LightComponent";
			constexpr const char* camera_component = "CameraComponent";
			constexpr const char* environment_component = "EnvironmentComponent";
			constexpr const char* sky_type = "Sky Type";
			constexpr const char* procedural = "Procedural";
			constexpr const char* fog_volume_component = "FogVolumeComponent";
			constexpr const char* ddgi_volume_component = "DDGIVolumeComponent";
			constexpr const char* geometry_component = "GeometryComponent";
			constexpr const char* collider_3d_component = "Collider3DComponent";
			constexpr const char* rigidbody_3d_component = "Rigidbody3DComponent";
			constexpr const char* motion_type = "Motion Type";
			constexpr const char* static_str = "Static";
			constexpr const char* kinematic = "Kinematic";
			constexpr const char* mass = "Mass";
			constexpr const char* friction = "Friction";
			constexpr const char* restitution = "Restitution";
			constexpr const char* gravity_factor = "Gravity Factor";
			constexpr const char* linear_velocity = "Linear Velocity";
			constexpr const char* angular_velocity = "Angular Velocity";
			constexpr const char* sprite_2d_component = "Sprite2DComponent";
			constexpr const char* text_2d_component = "Text2DComponent";
			constexpr const char* sprite_3d_component = "Sprite3DComponent";
			constexpr const char* text_3d_component = "Text3DComponent";
			constexpr const char* animation_component = "AnimationComponent";
			constexpr const char* material_component = "MaterialComponent";
			constexpr const char* script_component = "ScriptComponent";
			constexpr const char* script = "Script";
			constexpr const char* none_placeholder = "<none>";
			constexpr const char* script_already_exists = "script already exists on this component";
			constexpr const char* last_error_format = "Last Error: %s";
			constexpr const char* parent = "Parent";
			constexpr const char* position = "Position";
			constexpr const char* rotation_xyz = "Rotation XYZ";
			constexpr const char* scale = "Scale";
			constexpr const char* type = "Type";
			constexpr const char* color = "Color";
			constexpr const char* directional = "Directional";
			constexpr const char* point = "Point";
			constexpr const char* spot = "Spot";
			constexpr const char* intensity = "Intensity";
			constexpr const char* range = "Range";
			constexpr const char* outer_cone = "Outer Cone";
			constexpr const char* inner_cone = "Inner Cone";
			constexpr const char* shadow_resolution = "Shadow Resolution";
			constexpr const char* cascade_count = "Cascade Count";
			constexpr const char* cascade_lambda = "Cascade Lambda";
			constexpr const char* cascade_blend = "Cascade Blend";
			constexpr const char* near_plane = "Near";
			constexpr const char* far_plane = "Far";
			constexpr const char* fov_y = "FOV Y";
			constexpr const char* ortho_size = "Ortho Size";
			constexpr const char* aspect_ratio_format = "Aspect Ratio: %.3f";
			constexpr const char* aperture = "Aperture";
			constexpr const char* shutter_speed = "Shutter Speed";
			constexpr const char* sensitivity = "Sensitivity";
			constexpr const char* sun_direction = "Sun Direction";
			constexpr const char* sun_color = "Sun Color";
			constexpr const char* sun_intensity = "Sun Intensity";
			constexpr const char* sun_angular_radius = "Sun Angular Radius";
			constexpr const char* sun_glow_intensity = "Sun Glow Intensity";
			constexpr const char* sun_glow_falloff = "Sun Glow Falloff";
			constexpr const char* sky_intensity = "Sky Intensity";
			constexpr const char* sky_horizon_color = "Sky Horizon Color";
			constexpr const char* sky_horizon_falloff = "Sky Horizon Falloff";
			constexpr const char* sky_zenith_color = "Sky Zenith Color";
			constexpr const char* ground_intensity = "Ground Intensity";
			constexpr const char* ground_horizon_color = "Ground Horizon Color";
			constexpr const char* ground_falloff = "Ground Falloff";
			constexpr const char* ground_color = "Ground Color";
			constexpr const char* gi_mode = "GI Mode";
			constexpr const char* ambient = "Ambient";
			constexpr const char* ddgi = "DDGI";
			constexpr const char* ambient_color = "Ambient Color";
			constexpr const char* ambient_intensity = "Ambient Intensity";
			constexpr const char* indirect_diffuse_scale = "Indirect Diffuse Scale";
			constexpr const char* indirect_specular_scale = "Indirect Specular Scale";
			constexpr const char* probe_counts = "Probe Counts";
			constexpr const char* probe_spacing = "Probe Spacing";
			constexpr const char* volume_offset = "Volume Offset";
			constexpr const char* probes_per_frame = "Probes Per Frame";
			constexpr const char* priority = "Priority";
			constexpr const char* hysteresis = "Hysteresis";
			constexpr const char* normal_bias = "Normal Bias";
			constexpr const char* view_bias = "View Bias";
			constexpr const char* max_distance = "Max Distance";
			constexpr const char* mesh_format = "Mesh: %s";
			constexpr const char* trigger = "Trigger";
			constexpr const char* box = "Box";
			constexpr const char* sphere = "Sphere";
			constexpr const char* offset = "Offset";
			constexpr const char* half_extent = "Half Extent";
			constexpr const char* radius = "Radius";
			constexpr const char* anchor = "Anchor";
			constexpr const char* size = "Size";
			constexpr const char* pivot = "Pivot";
			constexpr const char* uv_rect = "UV Rect";
			constexpr const char* layer = "Layer";
			constexpr const char* font_format = "Font: %s";
			constexpr const char* pixel_height = "Pixel Height";
			constexpr const char* height = "Height";
			constexpr const char* clips_format = "Clips: %d";
			constexpr const char* clip = "Clip";
			constexpr const char* text = "Text";
			constexpr const char* play = "Play";
			constexpr const char* pause = "Pause";
			constexpr const char* speed = "Speed";
			constexpr const char* time = "Time";
			constexpr const char* duration_format = "Duration: %.3fs";
			constexpr const char* bone_matrices_format = "Bone Matrices: %d";
			constexpr const char* bone_matrix_offset_format = "Bone Matrix Offset: %u";
			constexpr const char* material_slots_format = "Material Slots: %d";
			constexpr const char* fork_material = "Fork Material";
			constexpr const char* save_material_asset = "Save as Material Asset";
			constexpr const char* won_material_file = "WonEngine Material";
			constexpr const char* save_material_failed = "Failed to save material: ";
			constexpr const char* slot_prefix = "Slot ";
			constexpr const char* selected_slot = "Selected Slot";
			constexpr const char* shader_type = "Material Type";
			constexpr const char* unlit = "Unlit";
			constexpr const char* pbr = "PBR";
			constexpr const char* blend_mode = "Blend Mode";
			constexpr const char* opaque = "Opaque";
			constexpr const char* alpha = "Alpha";
			constexpr const char* additive = "Additive";
			constexpr const char* premultiplied = "Premultiplied";
			constexpr const char* base_color = "Base Color";
			constexpr const char* metallic = "Metallic";
			constexpr const char* roughness = "Roughness";
			constexpr const char* reflectance = "Reflectance";
			constexpr const char* anisotropy = "Anisotropy";
			constexpr const char* sheen_color = "Sheen Color";
			constexpr const char* sheen_roughness = "Sheen Roughness";
			constexpr const char* clearcoat = "Clearcoat";
			constexpr const char* clearcoat_roughness = "Clearcoat Roughness";
			constexpr const char* textures = "Textures";
			constexpr const char* texture_status_format = "%s: %s";
			constexpr const char* project_saved = "Project saved: ";
			constexpr const char* project_loaded = "Project loaded: ";
			constexpr const char* project_created = "Project created: ";
			constexpr const char* save_project_failed = "Save project failed: ";
			constexpr const char* load_project_failed = "Load project failed: ";
			constexpr const char* create_project_failed = "Create project failed: ";
			constexpr const char* save_scene_failed = "Save scene failed: ";
			constexpr const char* scene_saved = "Scene saved: ";
			constexpr const char* load_scene_failed = "Load scene failed: ";
			constexpr const char* load_scene_warning = "Load scene warning: ";
			constexpr const char* scene_loaded = "Scene loaded: ";
			constexpr const char* content_browser_import_commit_failed = "Content Browser import commit failed: ";
			constexpr const char* content_browser_import_failed = "Content Browser import failed: ";
			constexpr const char* asset_importer_tool_not_found = "AssetImporterTool not found: ";
			constexpr const char* failed_register_component_type = "Failed to register component type: ";
			constexpr const char* failed_enable_plugin = "Failed to enable plugin: ";
			constexpr const char* plugin_disabled_next_restart = "Plugin will be disabled on next editor restart: ";
			constexpr const char* base_color_map = "Base Color Map";
			constexpr const char* normal_map = "Normal Map";
			constexpr const char* emissive_map = "Emissive Map";
			constexpr const char* opacity_map = "Opacity Map";
			constexpr const char* displacement_map = "Displacement Map";
			constexpr const char* occlusion_map = "Occlusion Map";
			constexpr const char* sheen_color_map = "Sheen Color Map";
			constexpr const char* sheen_roughness_map = "Sheen Roughness Map";
			constexpr const char* clearcoat_map = "Clearcoat Map";
			constexpr const char* clearcoat_roughness_map = "Clearcoat Roughness Map";
			constexpr const char* clearcoat_normal_map = "Clearcoat Normal Map";
			constexpr const char* anisotropy_map = "Anisotropy Map";
			constexpr const char* roughness_map = "Roughness Map";
			constexpr const char* metallic_map = "Metallic Map";
			constexpr const char* audio_source_component = "AudioSourceComponent";
			constexpr const char* audio_listener_component = "AudioListenerComponent";
			constexpr const char* sound_asset_path = "Sound Path";
			constexpr const char* volume = "Volume";
			constexpr const char* pitch = "Pitch";
			constexpr const char* min_distance = "Min Distance";
			constexpr const char* is_3d = "3D Spatial";
			constexpr const char* play_on_start = "Play On Start";
			constexpr const char* is_playing = "Playing";
		}

		// temp

		class PluginSystemAdapter : public ecs::System
		{
		public:
			PluginSystemAdapter(const std::shared_ptr<plugin::Plugin>& plugin_in, const plugin::system::Desc* desc_in)
				: plugin(plugin_in)
				, desc(desc_in)
			{
			}

			ecs::SystemExecutionPolicy GetExecutionPolicy() const override
			{
				return ecs::SystemExecutionPolicy::Synchronous;
			}

			void Update(ecs::Scene& scene, float delta_time) override
			{
				if (!plugin || !desc || !desc->Update)
				{
					return;
				}

				plugin::system::UpdateContext context = {};
				context.scene = &scene;
				context.delta_time = delta_time;
				desc->Update(plugin->GetHandle(), &context);
			}

		private:
			std::shared_ptr<plugin::Plugin> plugin;
			const plugin::system::Desc* desc = nullptr;
		};

		const char* GetTypeDisplayName(const won::TypeDesc* type_desc)
		{
			if (!type_desc)
			{
				return "";
			}
			if (type_desc->display_name && type_desc->display_name[0] != '\0')
			{
				return type_desc->display_name;
			}
			return type_desc->name ? type_desc->name : "";
		}

		bool IsDefaultComponent(won::TypeId type_id)
		{
			switch (type_id)
			{
			case reflection::TypeMeta<NameComponent>::type_id:
			case reflection::TypeMeta<TransformComponent>::type_id:
			case reflection::TypeMeta<HierarchyComponent>::type_id:
			case reflection::TypeMeta<CameraComponent>::type_id:
			case reflection::TypeMeta<LightComponent>::type_id:
			case reflection::TypeMeta<EnvironmentComponent>::type_id:
			case reflection::TypeMeta<FogVolumeComponent>::type_id:
			case reflection::TypeMeta<DDGIVolumeComponent>::type_id:
			case reflection::TypeMeta<GeometryComponent>::type_id:
			case reflection::TypeMeta<Collider3DComponent>::type_id:
			case reflection::TypeMeta<Rigidbody3DComponent>::type_id:
			case reflection::TypeMeta<Sprite2DComponent>::type_id:
			case reflection::TypeMeta<Sprite3DComponent>::type_id:
			case reflection::TypeMeta<Text2DComponent>::type_id:
			case reflection::TypeMeta<Text3DComponent>::type_id:
			case reflection::TypeMeta<AnimationComponent>::type_id:
			case reflection::TypeMeta<MaterialComponent>::type_id:
			case reflection::TypeMeta<ScriptComponent>::type_id:
			case reflection::TypeMeta<AudioSourceComponent>::type_id:
			case reflection::TypeMeta<AudioListenerComponent>::type_id:
				return true;
			default:
				return false;
			}
		}

		bool DrawComponentRemoveButton(const char* component_name, bool can_remove = true);

		bool DrawReflectedField(const won::FieldDesc& field, uint8* component_data, uint32 component_size)
		{
			if (field.struct_size < sizeof(won::FieldDesc) || (field.flags & won::FieldFlagEditable) == 0 || !component_data)
			{
				return false;
			}
			if (field.offset > component_size || field.size > component_size - field.offset)
			{
				return false;
			}

			void* value = component_data + field.offset;
			const char* label = field.name ? field.name : "";
			switch (field.value_type)
			{
			case won::ValueType::Bool:
				return ImGui::Checkbox(label, static_cast<bool*>(value));
			case won::ValueType::Int8:
				return ImGui::InputScalar(label, ImGuiDataType_S8, value);
			case won::ValueType::UInt8:
				return ImGui::InputScalar(label, ImGuiDataType_U8, value);
			case won::ValueType::Int16:
				return ImGui::InputScalar(label, ImGuiDataType_S16, value);
			case won::ValueType::UInt16:
				return ImGui::InputScalar(label, ImGuiDataType_U16, value);
			case won::ValueType::Int32:
				return ImGui::InputScalar(label, ImGuiDataType_S32, value);
			case won::ValueType::UInt32:
				return ImGui::InputScalar(label, ImGuiDataType_U32, value);
			case won::ValueType::Int64:
				return ImGui::InputScalar(label, ImGuiDataType_S64, value);
			case won::ValueType::UInt64:
				return ImGui::InputScalar(label, ImGuiDataType_U64, value);
			case won::ValueType::Float32:
				return ImGui::DragFloat(label, static_cast<float*>(value), 0.01f);
			case won::ValueType::Float64:
				return ImGui::InputScalar(label, ImGuiDataType_Double, value);
			case won::ValueType::Int32x2:
				return ImGui::InputScalarN(label, ImGuiDataType_S32, value, 2);
			case won::ValueType::Int32x3:
				return ImGui::InputScalarN(label, ImGuiDataType_S32, value, 3);
			case won::ValueType::Int32x4:
				return ImGui::InputScalarN(label, ImGuiDataType_S32, value, 4);
			case won::ValueType::UInt32x2:
				return ImGui::InputScalarN(label, ImGuiDataType_U32, value, 2);
			case won::ValueType::UInt32x3:
				return ImGui::InputScalarN(label, ImGuiDataType_U32, value, 3);
			case won::ValueType::UInt32x4:
				return ImGui::InputScalarN(label, ImGuiDataType_U32, value, 4);
			case won::ValueType::Float32x2:
				return ImGui::DragFloat2(label, static_cast<float*>(value), 0.01f);
			case won::ValueType::Float32x3:
				return ImGui::DragFloat3(label, static_cast<float*>(value), 0.01f);
			case won::ValueType::Float32x4:
				return ImGui::DragFloat4(label, static_cast<float*>(value), 0.01f);
			case won::ValueType::String:
			{
				String& text = *static_cast<String*>(value);
				char buffer[1024] = {};
				strncpy_s(buffer, text.c_str(), sizeof(buffer) - 1);
				if (ImGui::InputText(label, buffer, sizeof(buffer)))
				{
					text = buffer;
					return true;
				}
				return false;
			}
			default:
				ImGui::TextDisabled(editor_text::unsupported_format, label);
				return false;
			}
		}

		bool DrawReflectedComponent(ecs::Scene& scene, ecs::Entity entity, const won::TypeDesc* type_desc)
		{
			if (!type_desc || IsDefaultComponent(type_desc->type_id))
			{
				return false;
			}

			void* component = scene.GetComponent(entity, type_desc->type_id);
			if (!component)
			{
				return false;
			}

			const char* component_name = GetTypeDisplayName(type_desc);
			ImGui::PushID(type_desc->name ? type_desc->name : component_name);
			ImGui::TextUnformatted(component_name);
			const bool remove_component = DrawComponentRemoveButton(component_name);
			if (!remove_component && type_desc->fields && type_desc->field_count > 0)
			{
				uint8* component_data = static_cast<uint8*>(component);
				for (uint32 field_index = 0; field_index < type_desc->field_count; ++field_index)
				{
					DrawReflectedField(type_desc->fields[field_index], component_data, type_desc->size);
				}
			}
			ImGui::PopID();
			ImGui::Separator();
			return remove_component;
		}

		float3 QuaternionToEulerXYZDegrees(const float4& quaternion)
		{
			XMVECTOR xquaternion = XMVector4Normalize(XMLoadFloat4(&quaternion));
			XMMATRIX rotation_matrix = XMMatrixRotationQuaternion(xquaternion);
			float4x4 matrix = {};
			XMStoreFloat4x4(&matrix, rotation_matrix);

			float pitch = asin(std::clamp(-matrix._32, -1.0f, 1.0f));
			float yaw = atan2(matrix._31, matrix._33);
			float roll = atan2(matrix._12, matrix._22);

			return {
				math::RadiansToDegrees(pitch),
				math::RadiansToDegrees(yaw),
				math::RadiansToDegrees(roll)
			};
		}

		float4 EulerXYZDegreesToQuaternion(const float3& euler_xyz_degrees)
		{
			const float pitch = math::DegreesToRadians(euler_xyz_degrees.x);
			const float yaw = math::DegreesToRadians(euler_xyz_degrees.y);
			const float roll = math::DegreesToRadians(euler_xyz_degrees.z);

			float4 quaternion = {};
			XMStoreFloat4(&quaternion, XMQuaternionRotationRollPitchYaw(pitch, yaw, roll));
			return quaternion;
		}

		bool DrawComponentRemoveButton(const char* component_name, bool can_remove)
		{
			ImGui::SameLine();
			const float button_width = ImGui::CalcTextSize("X").x + ImGui::GetStyle().FramePadding.x * 2.0f;
			ImGui::SetCursorPosX((std::max)(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - button_width));
			if (!can_remove)
			{
				ImGui::BeginDisabled();
			}

			const bool pressed = ImGui::SmallButton("X");
			if (!can_remove)
			{
				ImGui::EndDisabled();
			}

			if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
			{
				ImGui::SetTooltip(editor_text::remove_tooltip_format, component_name);
			}

			if (pressed && can_remove)
			{
				ImGui::OpenPopup(editor_text::remove_component_popup);
			}

			bool remove_component = false;
			if (ImGui::BeginPopup(editor_text::remove_component_popup, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text(editor_text::remove_confirm_format, component_name);
				ImGui::TextDisabled(editor_text::action_cannot_be_undone);

				if (ImGui::Button(editor_text::remove))
				{
					remove_component = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button(editor_text::cancel))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			return remove_component;
		}

		bool AddImGuiFont(const std::string& font_folder_path, const std::string& font_file_name, bool merge_icon = true)
		{
			//PE: Add all lang.
			static const ImWchar generic_ranges_everything[] =
			{
			   0x0020, 0xFFFF, // Everything test.
			   0,
			};
			static const ImWchar generic_ranges_most_needed[] =
			{
				0x0020, 0x00FF, // Basic Latin + Latin Supplement
				0x0100, 0x017F,	//0100 - 017F  	Latin Extended-A
				0x0180, 0x024F,	//0180 - 024F  	Latin Extended-B
				0,
			};

			float FONTUPSCALE = 1.0; //Font upscaling.
			float FontSize = 18.0f;
			ImGuiIO& io = ImGui::GetIO();
			io.Fonts->Clear();

			std::string font_file_path = font_folder_path + "/" + font_file_name;
			ImFont* custom_font = io.Fonts->AddFontFromFileTTF(font_file_path.c_str(), FontSize * FONTUPSCALE, NULL, &generic_ranges_everything[0]); //Set as default font.
			if (custom_font && merge_icon)
			{
				std::string font_icon_path = editor_contents_root_dir + "Fonts/MaterialIcons-Regular.ttf";
				ImFontConfig config;
				config.MergeMode = true;
				config.GlyphOffset = ImVec2(0, 3);
				//config.GlyphMinAdvanceX = FontSize * FONTUPSCALE; // Use if you want to make the icon monospaced
				static const ImWchar icon_ranges[] = { ICON_MIN_MD, ICON_MAX_16_MD, 0 };
				io.Fonts->AddFontFromFileTTF(font_icon_path.c_str(), FontSize * FONTUPSCALE, &config, icon_ranges);

				return true;
			}

			custom_font = io.Fonts->AddFontDefault();

			return false;
		}

		void BuildDefaultDockLayout(ImGuiID dockspace_id, const ImVec2& size)
		{
			ImGui::DockBuilderRemoveNode(dockspace_id);
			ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspace_id, size);

			ImGuiID dock_main = dockspace_id;
			ImGuiID dock_right = 0;
			ImGuiID dock_bottom = 0;
			ImGuiID dock_left = 0;

			dock_right = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Right, 0.20f, nullptr, &dock_main);
			dock_bottom = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Down, 0.30f, nullptr, &dock_main);
			dock_left = ImGui::DockBuilderSplitNode(dock_main, ImGuiDir_Left, 0.20f, nullptr, &dock_main);

			ImGui::DockBuilderDockWindow(editor_text::viewport_window, dock_main);
			ImGui::DockBuilderDockWindow(editor_text::inspector_window, dock_right);
			ImGui::DockBuilderDockWindow(editor_text::contents_browser_window, dock_bottom);
			ImGui::DockBuilderDockWindow(editor_text::log_window, dock_bottom);
			ImGui::DockBuilderDockWindow(editor_text::profiler_window, dock_bottom);
			ImGui::DockBuilderDockWindow(editor_text::entity_list_window, dock_left);

			ImGui::DockBuilderFinish(dockspace_id);
		}

		bool ProjectWorldToScreen(const ecs::CameraComponent& camera, const ImVec2& viewport_pos, const ImVec2& viewport_size, const float3& world_position, ImVec2& out_screen_position)
		{
			const XMVECTOR projected = XMVector3Project(
				XMLoadFloat3(&world_position),
				viewport_pos.x,
				viewport_pos.y,
				viewport_size.x,
				viewport_size.y,
				0.0f,
				1.0f,
				XMLoadFloat4x4(&camera.projection),
				XMLoadFloat4x4(&camera.view),
				XMMatrixIdentity());

			float3 projected_position = {};
			XMStoreFloat3(&projected_position, projected);
			if (projected_position.z < 0.0f || projected_position.z > 1.0f)
			{
				return false;
			}

			out_screen_position = ImVec2(projected_position.x, projected_position.y);
			return true;
		}

		void DrawWorldLine(ImDrawList* draw_list, const ecs::CameraComponent& camera, const ImVec2& viewport_pos, const ImVec2& viewport_size, const float3& from, const float3& to, ImU32 color, float thickness = 1.0f)
		{
			ImVec2 from_screen = {};
			ImVec2 to_screen = {};
			if (!ProjectWorldToScreen(camera, viewport_pos, viewport_size, from, from_screen) ||
				!ProjectWorldToScreen(camera, viewport_pos, viewport_size, to, to_screen))
			{
				return;
			}

			draw_list->AddLine(from_screen, to_screen, color, thickness);
		}

		void DrawWorldAABB(ImDrawList* draw_list, const ecs::CameraComponent& camera, const ImVec2& viewport_pos, const ImVec2& viewport_size, const float3& bounds_min, const float3& bounds_max, ImU32 color, float thickness = 1.0f)
		{
			const float3 corners[8] = {
				{ bounds_min.x, bounds_min.y, bounds_min.z },
				{ bounds_max.x, bounds_min.y, bounds_min.z },
				{ bounds_min.x, bounds_max.y, bounds_min.z },
				{ bounds_max.x, bounds_max.y, bounds_min.z },
				{ bounds_min.x, bounds_min.y, bounds_max.z },
				{ bounds_max.x, bounds_min.y, bounds_max.z },
				{ bounds_min.x, bounds_max.y, bounds_max.z },
				{ bounds_max.x, bounds_max.y, bounds_max.z }
			};

			const int edges[12][2] = {
				{ 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 },
				{ 4, 5 }, { 5, 7 }, { 7, 6 }, { 6, 4 },
				{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
			};

			for (const auto& edge : edges)
			{
				DrawWorldLine(draw_list, camera, viewport_pos, viewport_size, corners[edge[0]], corners[edge[1]], color, thickness);
			}
		}

		void DrawTextBlock(ImDrawList* draw_list, const ImVec2& start_position, const Vector<String>& lines, bool align_right = false)
		{
			if (lines.empty())
			{
				return;
			}

			float max_width = 0.0f;
			for (const String& line : lines)
			{
				max_width = (std::max)(max_width, ImGui::CalcTextSize(line.c_str()).x);
			}

			const float line_height = ImGui::GetTextLineHeightWithSpacing();
			const float block_width = max_width + 16.0f;
			const float block_height = line_height * static_cast<float>(lines.size()) + 10.0f;
			const ImVec2 bg_min = align_right ? ImVec2(start_position.x - block_width - 4.0f, start_position.y + 4.0f) : start_position + ImVec2(4.0f, 4.0f);
			const ImVec2 bg_max = ImVec2(bg_min.x + block_width, bg_min.y + block_height);
			ImVec2 text_cursor = bg_min + ImVec2(6.0f, 6.0f);
			draw_list->AddRectFilled(bg_min, bg_max, IM_COL32(18, 18, 18, 180), 6.0f);
			draw_list->AddRect(bg_min, bg_max, IM_COL32(90, 90, 90, 200), 6.0f);

			for (const String& line : lines)
			{
				draw_list->AddText(text_cursor, IM_COL32(230, 230, 230, 255), line.c_str());
				text_cursor.y += line_height;
			}
		}

		void DrawDDGIDebugOverlay(
			const ecs::CameraComponent& camera,
			const rendering::RendererDebugState& renderer_debug_state,
			const ImVec2& viewport_pos,
			const ImVec2& viewport_size,
			bool show_volume,
			bool show_probes,
			bool show_text,
			int max_probe_draw_count)
		{
			ImDrawList* draw_list = ImGui::GetWindowDrawList();
			const rendering::RendererDebugDDGIState& ddgi_state = renderer_debug_state.ddgi;

			if (show_text)
			{
				auto bool_text = [](bool value) -> const char*
				{
					return value ? "Yes" : "No";
				};

				Vector<String> lines;
				lines.push_back("DDGI Debug");
				lines.push_back(String("GI Mode DDGI: ") + bool_text(ddgi_state.gi_mode_ddgi));
				lines.push_back(String("Active Volume: ") + bool_text(ddgi_state.volume_active));
				if (ddgi_state.volume_active)
				{
					lines.push_back("Volume Entity: " + std::to_string(ddgi_state.volume_entity));
					lines.push_back("Probe Counts: " + std::to_string(ddgi_state.probe_counts.x) + ", " + std::to_string(ddgi_state.probe_counts.y) + ", " + std::to_string(ddgi_state.probe_counts.z));
					lines.push_back("Probe Spacing: " + std::to_string(ddgi_state.probe_spacing.x) + ", " + std::to_string(ddgi_state.probe_spacing.y) + ", " + std::to_string(ddgi_state.probe_spacing.z));
					lines.push_back("Total Probes: " + std::to_string(ddgi_state.total_probe_count));
					lines.push_back("Debug Probe Samples: " + std::to_string(ddgi_state.probes.size()));
				}
				lines.push_back(String("Texture Allocated: ") + bool_text(ddgi_state.irradiance_texture_allocated));
				lines.push_back(String("SRV Valid: ") + bool_text(ddgi_state.irradiance_srv_valid) + " (" + std::to_string(ddgi_state.irradiance_texture_srv) + ")");
				lines.push_back(String("UAV Valid: ") + bool_text(ddgi_state.irradiance_uav_valid) + " (" + std::to_string(ddgi_state.irradiance_texture_uav) + ")");
				lines.push_back(String("Visibility Allocated: ") + bool_text(ddgi_state.visibility_texture_allocated));
				lines.push_back(String("Visibility SRV: ") + bool_text(ddgi_state.visibility_srv_valid) + " (" + std::to_string(ddgi_state.visibility_texture_srv) + ")");
				lines.push_back(String("Visibility UAV: ") + bool_text(ddgi_state.visibility_uav_valid) + " (" + std::to_string(ddgi_state.visibility_texture_uav) + ")");
				lines.push_back(String("Probe Data Allocated: ") + bool_text(ddgi_state.probe_data_buffer_allocated));
				lines.push_back(String("Probe Data SRV: ") + bool_text(ddgi_state.probe_data_srv_valid) + " (" + std::to_string(ddgi_state.probe_data_buffer_srv) + ")");
				lines.push_back(String("Probe Data UAV: ") + bool_text(ddgi_state.probe_data_uav_valid) + " (" + std::to_string(ddgi_state.probe_data_buffer_uav) + ")");
				lines.push_back(String("History Valid: ") + bool_text(ddgi_state.history_valid));
				lines.push_back(String("Pipeline Ready: ") + bool_text(ddgi_state.probe_update_pipeline_ready));
				lines.push_back(String("Probe Dispatch: ") + bool_text(ddgi_state.probe_update_dispatched));
				lines.push_back("Dispatch Groups: " + std::to_string(ddgi_state.dispatch_groups.x) + ", " + std::to_string(ddgi_state.dispatch_groups.y) + ", " + std::to_string(ddgi_state.dispatch_groups.z));

				DrawTextBlock(draw_list, ImVec2(viewport_pos.x + viewport_size.x, viewport_pos.y), lines, true);
			}
		}
	}

	void EditorApplication::EditorViewport::CameraController::Update(const ecs::CameraComponent& camera, ecs::TransformComponent& transform, float dt, const float2& viewport_mouse_pos, const float2& viewport_size, bool can_begin_interaction)
	{
		XMVECTOR xforward = XMVector3Normalize(XMLoadFloat3(&camera.forward));
		XMVECTOR xup = XMVector3Normalize(XMLoadFloat3(&camera.up));
		XMVECTOR xright = XMVector3Normalize(XMVector3Cross(xup, xforward));

		if (can_begin_interaction && io::IsDown(io::Button('W')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xforward, dt * move_speed));
			transform.Translate(translation);
		}
		if (can_begin_interaction && io::IsDown(io::Button('A')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xright, -dt * move_speed));
			transform.Translate(translation);
		}
		if (can_begin_interaction && io::IsDown(io::Button('S')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xforward, -dt * move_speed));
			transform.Translate(translation);
		}
		if (can_begin_interaction && io::IsDown(io::Button('D')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xright, dt * move_speed));
			transform.Translate(translation);
		}

		if (can_begin_interaction && io::IsPressed(io::Button::MOUSE_BUTTON_RIGHT))
		{
			BeginInteraction(InteractionMode::Rotate, transform, viewport_mouse_pos);
		}
		else if (can_begin_interaction && io::IsPressed(io::Button::MOUSE_BUTTON_MIDDLE))
		{
			BeginInteraction(InteractionMode::Orbit, transform, viewport_mouse_pos);
		}

		bool can_update_interaction = false;
		bool interaction_finished = false;
		if (active_interaction == InteractionMode::PanMove)
		{
			interaction_finished = true;
		}
		else if (active_interaction == InteractionMode::Rotate)
		{
			can_update_interaction = io::IsDown(io::Button::MOUSE_BUTTON_RIGHT);
			interaction_finished = !can_update_interaction;
		}
		else if (active_interaction == InteractionMode::Orbit)
		{
			can_update_interaction = io::IsDown(io::Button::MOUSE_BUTTON_MIDDLE);
			interaction_finished = !can_update_interaction;
		}

		if (can_update_interaction)
		{
			UpdateInteraction(transform, viewport_mouse_pos, viewport_size);
		}

		if (interaction_finished)
		{
			EndInteraction();
		}
	}

	void EditorApplication::EditorViewport::CameraController::BeginInteraction(InteractionMode mode, const ecs::TransformComponent& transform, const float2& viewport_mouse_pos)
	{
		pressed = true;
		active_interaction = mode;

		const XMVECTOR base_forward = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		const XMVECTOR base_right = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
		const XMVECTOR cam_rotation = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));
		const XMVECTOR cam_forward = XMVector3Rotate(base_forward, cam_rotation);
		const XMVECTOR cam_right = XMVector3Rotate(base_right, cam_rotation);
		pitch = std::asin(math::Clamp(XMVectorGetY(cam_forward), -1.0f, 1.0f));
		const float cos_pitch = std::cos(pitch);
		if (std::abs(cos_pitch) > 0.0001f)
		{
			yaw = std::atan2(XMVectorGetX(cam_forward), XMVectorGetZ(cam_forward));
		}
		else
		{
			yaw = std::atan2(-XMVectorGetZ(cam_right), XMVectorGetX(cam_right));
		}

		const XMVECTOR cam_pos = XMLoadFloat3(&transform.position);
		const XMVECTOR xfocus_point = XMLoadFloat3(&focus_point);
		orbit_distance = XMVectorGetX(XMVector3Length(cam_pos - xfocus_point));
		prev_mouse_pos = viewport_mouse_pos;
	}

	void EditorApplication::EditorViewport::CameraController::UpdateInteraction(ecs::TransformComponent& transform, const float2& viewport_mouse_pos, const float2& viewport_size)
	{
		if (!pressed)
		{
			return;
		}

		float2 mouse_delta = { viewport_mouse_pos.x - prev_mouse_pos.x, viewport_mouse_pos.y - prev_mouse_pos.y };
		if (math::FloatEqual(mouse_delta.x, 0.f) && math::FloatEqual(mouse_delta.y, 0.f))
		{
			return;
		}

		const float viewport_width = (std::max)(viewport_size.x, 1.0f);
		const float viewport_height = (std::max)(viewport_size.y, 1.0f);
		const float dx = mouse_delta.x / viewport_width;
		const float dy = -mouse_delta.y / viewport_height;
		constexpr float pitch_limit = XM_PIDIV2 - 0.001f;

		if (active_interaction == InteractionMode::PanMove)
		{
			const XMVECTOR cam_pos = XMLoadFloat3(&transform.position);
			const XMVECTOR cam_rotation = XMQuaternionNormalize(XMLoadFloat4(&transform.rotation));
			const XMVECTOR cam_right = XMVector3Rotate(XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f), cam_rotation);
			const XMVECTOR cam_up = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), cam_rotation);
			const XMVECTOR xfocus_point = XMLoadFloat3(&focus_point);
			const float distance = XMVectorGetX(XMVector3Length(cam_pos - xfocus_point));
			const XMVECTOR delta = XMVectorAdd(XMVectorScale(cam_right, -dx * distance), XMVectorScale(cam_up, -dy * distance));
			XMStoreFloat3(&transform.position, XMVectorAdd(cam_pos, delta));
		}
		else if (active_interaction == InteractionMode::Rotate || active_interaction == InteractionMode::Orbit)
		{
			const float speed = active_interaction == InteractionMode::Orbit ? orbit_speed : rotate_speed;
			yaw += dx * XM_2PI * speed;
			pitch = math::Clamp(pitch + dy * XM_2PI * speed, -pitch_limit, pitch_limit);

			const float cos_pitch = std::cos(pitch);
			const XMVECTOR cam_forward = XMVectorSet(std::sin(yaw) * cos_pitch, std::sin(pitch), std::cos(yaw) * cos_pitch, 0.0f);
			const XMVECTOR cam_right = XMVectorSet(std::cos(yaw), 0.0f, -std::sin(yaw), 0.0f);
			const XMVECTOR cam_up = XMVector3Normalize(XMVector3Cross(cam_forward, cam_right));

			XMMATRIX cam_world = XMMatrixIdentity();
			cam_world.r[0] = XMVectorSetW(cam_right, 0.0f);
			cam_world.r[1] = XMVectorSetW(cam_up, 0.0f);
			cam_world.r[2] = XMVectorSetW(cam_forward, 0.0f);

			XMStoreFloat4(&transform.rotation, XMQuaternionNormalize(XMQuaternionRotationMatrix(cam_world)));
			if (active_interaction == InteractionMode::Orbit)
			{
				const XMVECTOR xfocus_point = XMLoadFloat3(&focus_point);
				const XMVECTOR cam_pos = xfocus_point - XMVectorScale(cam_forward, (std::max)(orbit_distance, 0.001f));
				XMStoreFloat3(&transform.position, cam_pos);
			}
		}

		transform.SetDirty();
		prev_mouse_pos = viewport_mouse_pos;
	}

	void EditorApplication::EditorViewport::CameraController::EndInteraction()
	{
		pressed = false;
		active_interaction = InteractionMode::None;
	}

	void EditorApplication::Initialize(const ApplicationDesc& desc)
	{
		project::ProjectSettings empty_project_settings = {};
		Initialize(desc, empty_project_settings);
	}

	void EditorApplication::Initialize(const ApplicationDesc& desc, const project::ProjectSettings& loaded_project_settings_in)
	{
		std::shared_ptr<platform::SplashWindow> splash = nullptr;
		if (desc.project_settings.splash_enabled)
		{
			platform::SplashWindowDesc splash_desc = {};
			splash_desc.title = desc.project_settings.splash_title.c_str();
			splash_desc.status = desc.project_settings.splash_status.c_str();
			splash_desc.style.title_top = 382;
			splash_desc.style.title_height = 52;
			splash_desc.style.status_top = 444;
			splash_desc.style.status_height = 32;
			String splash_image_path;
			if (!desc.project_settings.splash_image.empty())
			{
				splash_image_path = project::ResolveProjectContentPath(project::GetContentRoot(desc.project_settings), desc.project_settings.splash_image);
				splash_desc.image_path = splash_image_path.c_str();
			}
			splash = platform::CreateSplashWindow(splash_desc);
		}

		loaded_project_settings = loaded_project_settings_in;
		const bool defer_main_window_show = splash && desc.project_settings.window_visible;
		if (splash)
		{
			splash->SetStatus(editor_text::splash_starting_renderer);
		}
		ApplicationDesc initialize_desc = desc;
		initialize_desc.defer_window_show = defer_main_window_show;
		Application::Initialize(initialize_desc);
		if (splash)
		{
			splash->SetStatus(editor_text::splash_loading_editor_settings);
		}
		contents_root_dir = project::GetContentRoot(loaded_project_settings);
		contents_root_dir = io::NormalizePath(contents_root_dir);
		if (!contents_root_dir.empty() && contents_root_dir.back() != '/')
		{
			contents_root_dir += "/";
		}
		if (!contents_root_dir.empty())
		{
			std::error_code error;
			std::filesystem::current_path(std::filesystem::u8path(contents_root_dir), error);
		}
		editor_contents_root_dir = io::NormalizePath(String(CONTENTS_ROOT_DIR));
		if (!editor_contents_root_dir.empty() && editor_contents_root_dir.back() != '/')
		{
			editor_contents_root_dir += "/";
		}
		editor_settings.settings_path = io::CombinePath(io::GetExecutableDirectory(), editor_settings_file_name);
		LoadEditorSettings();

		ecs::SceneDesc scene_desc = {};
		scene_desc.script_runtime = script_runtime.get();
		scene_desc.physics = project::GetPhysicsDesc(project_settings);
		scene_desc.audio_mixer = audio_mixer.get();
		loaded_scene = ecs::Scene(scene_desc);
		rendering::View editor_view = {};
		editor_view.scene = &loaded_scene;
		editor_view.options.resize_policy = rendering::ViewResizePolicy::Manual;
		editor_view.options.update_camera_aspect = false;
		editor_view.viewport.width = project_settings.window_width;
		editor_view.viewport.height = project_settings.window_height;
		editor_view.scissor.width = project_settings.window_width;
		editor_view.scissor.height = project_settings.window_height;
		uint32 editor_view_index = AddView(editor_view);
		editor_viewport.view = &GetView(editor_view_index);

		{
			ShaderCompilerOptions compiler_options;
			compiler_options.backend = ShaderCompilerBackend::DXC;
			compiler_options.shader_source_root_path = editor_contents_root_dir + "CustomShaders";
			std::shared_ptr<ShaderCompiler> compiler = CreateShaderCompiler(compiler_options);

			ShaderCompileDesc compile_desc;
			compile_desc.stage = RHIShaderStage::Vertex;
			compile_desc.source_file_name = "ImGuiVS.hlsl";
			ShaderCompileResult compile_result = compiler->Compile(compile_desc);

			imgui_vs = { RHIShaderStage::Vertex, compile_result.bytecode.data(), compile_result.bytecode.size()};
			
			compile_desc.stage = RHIShaderStage::Pixel;
			compile_desc.source_file_name = "ImGuiPS.hlsl";
			compile_result = compiler->Compile(compile_desc);

			imgui_ps = { RHIShaderStage::Pixel, compile_result.bytecode.data(), compile_result.bytecode.size() };

			compile_desc.stage = RHIShaderStage::Vertex;
			compile_desc.source_file_name = "EditorGridVS.hlsl";
			compile_result = compiler->Compile(compile_desc);

			editor_grid_vs = { RHIShaderStage::Vertex, compile_result.bytecode.data(), compile_result.bytecode.size() };

			compile_desc.stage = RHIShaderStage::Pixel;
			compile_desc.source_file_name = "EditorGridPS.hlsl";
			compile_result = compiler->Compile(compile_desc);

			editor_grid_ps = { RHIShaderStage::Pixel, compile_result.bytecode.data(), compile_result.bytecode.size() };
		}

		
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
		//io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // IF using Docking Branch

		// Setup Dear ImGui style
		//ImGui::StyleColorsDark();
		theme::SetupVisualStudioStyle();

		ImGuiStyle& style = ImGui::GetStyle();
		style.WindowMenuButtonPosition = ImGuiDir_None;

#ifdef _WIN32
		window->SetPlatformMessageHandler([](void* hwnd, uint32 message, Size wparam, Size lparam) -> bool
			{
				return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam)) != 0;
			});
		ImGui_ImplWin32_Init(window->GetNativeHandle());
#endif

		std::string font_folder_path = editor_contents_root_dir + "Fonts";
		AddImGuiFont(font_folder_path + "/Noto_Sans_KR/static", "NotoSansKR-Regular.ttf");

		InitImGui();
		InitEditorGrid();

		if (splash)
		{
			splash->SetStatus(editor_text::splash_loading_plugins);
		}
		LoadPlugins();

		if (splash)
		{
			splash->SetStatus(editor_text::splash_loading_startup_scene);
		}
		bool startup_scene_loaded = false;
		String startup_scene_path = loaded_project_settings.startup_scene;
		if (!startup_scene_path.empty())
		{
			startup_scene_path = project::ResolveProjectContentPath(contents_root_dir, startup_scene_path);
			if (io::IsFile(startup_scene_path))
			{
				LoadScene(startup_scene_path);
				startup_scene_loaded = true;
			}
		}
		if (!startup_scene_loaded)
		{
			CreateStartupScene();
		}
		contents_watcher = io::CreateDirectoryWatcher(contents_root_dir, true);
		contents_watcher_poll_timer = 0.0f;

		//main_viewport_pos = { 0, 0 };
		//main_viewport_size = { static_cast<float>(editor_viewport.view->viewport.width), static_cast<float>(editor_viewport.view->viewport.height) };
		UpdateEntityList();
		if (defer_main_window_show)
		{
			ShowMainWindow();
		}
		if (splash)
		{
			splash->Close();
		}
	}

	void EditorApplication::Shutdown()
	{
		SaveEditorSettings();
		if (!loaded_project_settings.settings_path.empty())
		{
			project::SaveSettings(loaded_project_settings.settings_path, loaded_project_settings);
		}
		WaitIdle();

		imgui_pso.reset();
		editor_grid_pso.reset();
		imgui_font.reset();
		imgui_font_subresource = {};
		imgui_sampler.reset();
		editor_viewport.debug_primitive_mesh.reset();
		editor_viewport.deferred_res_removals.clear();
		editor_viewport.camera_controller = {};
		for (const std::shared_ptr<EditorAssetImporter::ImportTask>& task : asset_importer.tasks)
		{
			if (!task)
			{
				continue;
			}

			jobsystem::Wait(task->context);
		}
		asset_importer.tasks.clear();
		contents_watcher.reset();
		contents_watcher_poll_timer = 0.0f;
		editor_viewport.debug_primitive_entity = ecs::INVALID_ENTITY;
		if (editor_viewport.view)
		{
			editor_viewport.view->scene = nullptr;
			editor_viewport.view->camera_entity = ecs::INVALID_ENTITY;
		}
		loaded_scene = {};
		editor_viewport.view = nullptr;
		plugins.clear();

		Application::Shutdown();
	}

	void EditorApplication::Update(float dt)
	{
		editor_viewport.renderer_debug_state = renderer ? renderer->GetDebugState() : rendering::RendererDebugState{};

		for (auto it = editor_viewport.deferred_res_removals.begin(); it != editor_viewport.deferred_res_removals.end();)
		{
			if (it->frames_left > 0)
			{
				--it->frames_left;
			}
			if (it->frames_left == 0)
			{
				it = editor_viewport.deferred_res_removals.erase(it);
			}
			else
			{
				++it;
			}
		}

		bool entity_list_dirty = false;
		for (auto it = asset_importer.tasks.begin(); it != asset_importer.tasks.end();)
		{
			std::shared_ptr<EditorAssetImporter::ImportTask> task = *it;
			if (!task)
			{
				it = asset_importer.tasks.erase(it);
				continue;
			}

			if (!task->finished.load())
			{
				++it;
				continue;
			}

			if (!task->failed.load())
			{
				if (CommitAssetImportResult(*task))
				{
					entity_list_dirty = true;
				}
				else
				{
					backlog::Post(editor_text::content_browser_import_commit_failed + task->path, backlog::LogLevel::Warning);
				}
			}
			else
			{
				backlog::Post(editor_text::content_browser_import_failed + task->path, backlog::LogLevel::Warning);
			}

			it = asset_importer.tasks.erase(it);
		}
		if (entity_list_dirty)
		{
			UpdateEntityList();
		}

		contents_watcher_poll_timer -= dt;
		if (contents_watcher && contents_watcher_poll_timer <= 0.0f)
		{
			contents_watcher_poll_timer = 0.5f;
			Vector<String> changed_script_paths;
			Vector<io::DirectoryWatcher::FileChange> file_changes;
			contents_watcher->Poll(&file_changes);
			for (const io::DirectoryWatcher::FileChange& change : file_changes)
			{
				const String ext = won::utils::ToLower(io::GetExtension(change.path));
				if (ext == resource::lua_script_file_extension)
				{
					const String changed_script_path = io::GetRelativePath(contents_root_dir, change.path);
					if (!changed_script_path.empty())
					{
						changed_script_paths.push_back(changed_script_path);
					}
				}
				else if (ext == resource::game_data_schema_extension)
				{
				    game_data_editor.loaded_schema_path.clear();
				}
			}

			auto script_array = editor_viewport.view->scene->GetComponentArray<ecs::ScriptComponent>();
			if (script_runtime && script_array)
			{
				for (const String& changed_script_path : changed_script_paths)
				{
					String reload_error;
					const bool reloaded = script_runtime->ReloadScript(changed_script_path, reload_error);
					for (Size i = 0; i < script_array->GetSize(); ++i)
					{
						const ecs::Entity entity = script_array->index_to_entity[i];
						ecs::ScriptComponent& script_component = script_array->data[i];
						script::ScriptCallContext context = {};
						context.scene = editor_viewport.view->scene;
						context.entity = entity;

						for (ecs::ScriptSlot& script_slot : script_component.scripts)
						{
							if (script_slot.script_path != changed_script_path)
							{
								continue;
							}

							if (!reloaded)
							{
								script_slot.last_error = reload_error;
								continue;
							}

							if (script_slot.instance.IsValid())
							{
								script::ScriptCallDesc call_desc = {};
								call_desc.type = script::ScriptCallType::OnDestroy;
								call_desc.context = context;
								script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error);
								script_runtime->DestroyInstance(script_slot.instance);
								script_slot.instance = {};
							}

							script::ScriptInstanceDesc desc = {};
							desc.script_path = script_slot.script_path;
							if (!script_runtime->CreateInstance(desc, script_slot.instance, script_slot.last_error))
							{
								script_slot.initialized = false;
								continue;
							}

							script_slot.initialized = false;
							script::ScriptCallDesc call_desc = {};
							call_desc.type = script::ScriptCallType::OnCreate;
							call_desc.context = context;
							if (script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error))
							{
								script_slot.initialized = true;
							}
						}
					}
				}
			}
		}

		if (renderer)
		{
			rendering::RendererDebugOptions debug_options = {};
			debug_options.ddgi_debug_enable = editor_viewport.debug_settings.show_ddgi_overlay;
			debug_options.bvh_debug_enable = editor_viewport.debug_settings.show_bvh_debug;
			debug_options.wireframe_enable = editor_viewport.debug_settings.use_wireframe;
			renderer->SetDebugOptions(debug_options);
		}
		UpdateDebugPrimitiveMesh();

		if (won::io::IsPressed(io::Button('R')))
		{
			renderer->ReloadShaders();
		}

		auto camera = editor_viewport.view->scene->GetComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity);
		auto transform = editor_viewport.view->scene->GetComponent<ecs::TransformComponent>(editor_viewport.view->camera_entity);
		if (!camera || !transform)
		{
			return;
		}

		float2 mouse_pos = io::GetMouseState().position;
		float2 main_viewport_pos = { (float)editor_viewport.view->viewport.x, (float)editor_viewport.view->viewport.y};
		float2 main_viewport_size = { (float)editor_viewport.view->viewport.width, (float)editor_viewport.view->viewport.height};
		float2 viewport_mouse_pos = { mouse_pos.x - main_viewport_pos.x, mouse_pos.y - main_viewport_pos.y };
		const bool can_control_viewport =
			editor_viewport.input_enabled &&
			0 <= viewport_mouse_pos.x && viewport_mouse_pos.x <= main_viewport_size.x &&
			0 <= viewport_mouse_pos.y && viewport_mouse_pos.y <= main_viewport_size.y;

		if (can_control_viewport && io::IsPressed(io::Button::MOUSE_BUTTON_LEFT))
		{
			ecs::RayCastHit hit = {};
			if (editor_viewport.view->RayCast(mouse_pos, hit, true))
			{
				editor_viewport.picked_entity = hit.entity;
			}
			else
			{
				editor_viewport.picked_entity = ecs::INVALID_ENTITY;
			}
		}

		editor_viewport.camera_controller.Update(*camera, *transform, dt, viewport_mouse_pos, main_viewport_size, can_control_viewport);
		Application::Update(dt);

	}

	void EditorApplication::LoadPlugins()
	{
		plugins.clear();
		asset_importer.tasks.clear();

		const String plugin_root_path = io::CombinePath(io::GetExecutableDirectory(), "Plugins");
		Vector<plugin::PluginInfo> plugin_list = plugin::ScanPluginList(plugin_root_path);
		String enabled_tokens = ";";
		for (const String& plugin_id : loaded_project_settings.enabled_plugins)
		{
			enabled_tokens += plugin_id;
			enabled_tokens += ";";
		}
		for (const plugin::PluginInfo& plugin_info : plugin_list)
		{
			EditorPluginInfo editor_plugin_info = {};
			editor_plugin_info.info = plugin_info;
			editor_plugin_info.enabled = editor_plugin_info.info.type == plugin::PluginType::EditorDefault || enabled_tokens.find(";" + editor_plugin_info.info.plugin_id + ";") != String::npos;
			if (editor_plugin_info.enabled)
			{
				editor_plugin_info.plugin = plugin::LoadPlugin(editor_plugin_info.info);
			}
			if (editor_plugin_info.plugin)
			{
				RegisterPluginExtensions(editor_plugin_info.plugin);
				editor_plugin_info.registered = true;
			}

			plugins.push_back(editor_plugin_info);
		}
	}

	void EditorApplication::RegisterPluginExtensions(const std::shared_ptr<plugin::Plugin>& plugin)
	{
		if (!plugin)
		{
			return;
		}

		for (const plugin::PluginExtension& extension : plugin->GetExtensions())
		{
			if (extension.extension_type == component::ExtensionType)
			{
				if (!extension.descriptor)
				{
					continue;
				}

				const auto* desc = static_cast<const component::Desc*>(extension.descriptor);
				if (!desc || desc->struct_size < sizeof(component::Desc) || desc->type_id == 0 || desc->size == 0 || desc->alignment == 0)
				{
					continue;
				}

				if (!reflection::RegisterType(desc))
				{
					backlog::Post(editor_text::failed_register_component_type + extension.extension_id, backlog::LogLevel::Warning);
					continue;
				}

				loaded_scene.RegisterComponent(desc);
				continue;
			}

			if (extension.extension_type == plugin::system::ExtensionType)
			{
				if (!extension.descriptor)
				{
					continue;
				}

				const auto* desc = static_cast<const plugin::system::Desc*>(extension.descriptor);
				if (!desc || desc->struct_size < sizeof(plugin::system::Desc) || !desc->Update)
				{
					continue;
				}

				loaded_scene.AddSystem(std::make_shared<PluginSystemAdapter>(plugin, desc));
			}
		}
	}

	void EditorApplication::SetPluginEnabled(Size plugin_index, bool enabled)
	{
		if (plugin_index >= plugins.size())
		{
			return;
		}

		EditorPluginInfo& plugin_info = plugins[plugin_index];
		if (plugin_info.info.type == plugin::PluginType::EditorDefault)
		{
			plugin_info.enabled = true;
			return;
		}

		if (plugin_info.enabled == enabled)
		{
			return;
		}

		plugin_info.enabled = enabled;
		if (enabled && !plugin_info.plugin)
		{
			plugin_info.plugin = plugin::LoadPlugin(plugin_info.info);
			if (!plugin_info.plugin)
			{
				plugin_info.enabled = false;
				backlog::Post(editor_text::failed_enable_plugin + plugin_info.info.plugin_id, backlog::LogLevel::Warning);
			}
			else if (!plugin_info.registered)
			{
				RegisterPluginExtensions(plugin_info.plugin);
				plugin_info.registered = true;
			}
		}
		else if (!enabled && plugin_info.plugin)
		{
			backlog::Post(editor_text::plugin_disabled_next_restart + plugin_info.info.plugin_id);
		}

		auto plugin_it = std::find(loaded_project_settings.enabled_plugins.begin(), loaded_project_settings.enabled_plugins.end(), plugin_info.info.plugin_id);
		if (plugin_info.enabled)
		{
			if (plugin_it == loaded_project_settings.enabled_plugins.end())
			{
				loaded_project_settings.enabled_plugins.push_back(plugin_info.info.plugin_id);
			}
		}
		else if (plugin_it != loaded_project_settings.enabled_plugins.end())
		{
			loaded_project_settings.enabled_plugins.erase(plugin_it);
		}

	}

	uint64 EditorApplication::StartAssetImport(const String& path)
	{
		const String tool_path = io::CombinePath(io::GetExecutableDirectory(), "AssetImporterTool.exe");
		if (path.empty() || !io::IsFile(tool_path))
		{
			backlog::Post(editor_text::asset_importer_tool_not_found, backlog::LogLevel::Warning);
			return 0;
		}

		static uint64 import_task_counter = 0;
		auto task = std::make_shared<EditorAssetImporter::ImportTask>();
		task->path = path;
		task->id = ++import_task_counter;
		task->context.priority = jobsystem::Priority::Streaming;
		asset_importer.tasks.push_back(task);

		const String settings_path = loaded_project_settings.settings_path;
		jobsystem::Execute(task->context, [task, tool_path, settings_path](jobsystem::JobArgs)
		{
			String cmd = "\"" + tool_path + "\" \"" + settings_path + "\" \"" + task->path + "\"";
			std::vector<char> cmd_buf(cmd.begin(), cmd.end());
			cmd_buf.push_back('\0');

			STARTUPINFOA si = {};
			si.cb = sizeof(si);
			PROCESS_INFORMATION pi = {};
			if (!CreateProcessA(nullptr, cmd_buf.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
			{
				task->failed.store(true);
				task->finished.store(true);
				return;
			}

			CloseHandle(pi.hThread);
			WaitForSingleObject(pi.hProcess, INFINITE);

			DWORD exit_code = 0;
			GetExitCodeProcess(pi.hProcess, &exit_code);
			CloseHandle(pi.hProcess);

			if (exit_code != 0)
			{
				task->failed.store(true);
			}
			task->finished.store(true);
		});
		return task->id;
	}

	bool EditorApplication::CommitAssetImportResult(EditorAssetImporter::ImportTask& task)
	{
		if (!editor_viewport.view || !editor_viewport.view->scene || !device)
		{
			return false;
		}

		resource::AssetMeta meta = {};
		if (!resource::LoadAssetMeta(resource::GetAssetMetaPath(task.path), meta) || meta.binary_path.empty())
		{
			return false;
		}

		auto mesh = resource::LoadMeshBinary(io::CombinePath(contents_root_dir, meta.binary_path));
		if (!mesh || !mesh->IsValid() || !rendering::utils::CreateRenderData(*device, *mesh))
		{
			return false;
		}

		const String mat_binary_path = String(generated_asset_directory) + "/" + meta.asset_id + "." + resource::material_binary_extension;
		auto material_resource = resource::LoadMaterialBinary(io::CombinePath(contents_root_dir, mat_binary_path));
		if (!material_resource)
		{
			material_resource = std::make_shared<resource::Material>();
		}
		if (material_resource->slots.empty())
		{
			material_resource->slots.emplace_back();
		}

		for (resource::MaterialSlot& slot : material_resource->slots)
		{
			for (uint32 tex_index = 0; tex_index < TEXTURESLOT_COUNT; ++tex_index)
			{
				resource::MaterialSlot::TextureMap& texture_map = slot.textures[tex_index];
				if (texture_map.texture_asset_path.empty())
				{
					continue;
				}
				auto image = resource::LoadTextureBinary(io::CombinePath(contents_root_dir, texture_map.texture_asset_path));
				if (!image || !image->IsValid())
				{
					continue;
				}
				if (rendering::utils::CreateRenderData(*device, *image, image->format, false))
				{
					texture_map.texture = image->render_data.texture;
					texture_map.res_handle = image->render_data.srv;
				}
			}
		}

		ecs::Scene* scene = editor_viewport.view->scene;
		const ecs::Entity root_entity = scene->CreateEntity();
		if (auto* transform = scene->AddComponent<ecs::TransformComponent>(root_entity))
		{
			transform->SetDirty();
		}
		if (auto* name = scene->AddComponent<ecs::NameComponent>(root_entity))
		{
			name->value = meta.asset_name.empty() ? io::GetFilename(task.path) : meta.asset_name;
		}
		if (auto* material = scene->AddComponent<ecs::MaterialComponent>(root_entity))
		{
			material->SetMaterialAssetPath(mat_binary_path);
			material->SetMaterial(material_resource);
		}
		if (auto* geometry = scene->AddComponent<ecs::GeometryComponent>(root_entity))
		{
			geometry->mesh_asset_path = meta.binary_path;
			geometry->SetMesh(mesh);
			geometry->SetCastShadow(true);
		}
		if (mesh->skeleton && mesh->skeleton->IsValid() && !mesh->animation_clips.empty())
		{
			if (auto* animation = scene->AddComponent<ecs::AnimationComponent>(root_entity))
			{
				animation->clips = mesh->animation_clips;
			}
		}

		scene->SetBVHDirty();
		return true;
	}
	void EditorApplication::UpdateDebugPrimitiveMesh()
	{
		bool created_debug_primitive_entity = false;
		if (editor_viewport.debug_primitive_entity == ecs::INVALID_ENTITY || !editor_viewport.debug_primitive_mesh)
		{
			editor_viewport.debug_primitive_entity = editor_viewport.view->scene->CreateEntity();
			editor_viewport.debug_primitive_mesh = std::make_shared<resource::Mesh>();
			created_debug_primitive_entity = true;

			editor_viewport.view->scene->AddComponent<ecs::TransformComponent>(editor_viewport.debug_primitive_entity);

			if (auto geometry = editor_viewport.view->scene->AddComponent<ecs::GeometryComponent>(editor_viewport.debug_primitive_entity))
			{
				geometry->SetMesh(editor_viewport.debug_primitive_mesh);
				geometry->SetExcludeFromBVH(true);
			}

			editor_viewport.view->scene->AddComponent<ecs::MaterialComponent>(editor_viewport.debug_primitive_entity);

			if (auto name = editor_viewport.view->scene->AddComponent<ecs::NameComponent>(editor_viewport.debug_primitive_entity))
			{
				name->value = "Debug Primitives";
			}
		}
		if (created_debug_primitive_entity)
		{
			UpdateEntityList();
		}

		if (!editor_viewport.debug_primitive_mesh)
		{
			return;
		}

		enum EditorPrimitiveMaterialSlot : uint32
		{
			EditorPrimitiveDDGIVolume,
			EditorPrimitiveDDGIProbe,
			EditorPrimitiveDDGIProbeRelocated,
			EditorPrimitiveDDGIProbeInvalid,
			EditorPrimitiveCPUBVHInternal,
			EditorPrimitiveCPUBVHLeaf,
			EditorPrimitiveGPUBVHInternal,
			EditorPrimitiveGPUBVHLeaf,
			EditorPrimitiveCollider3D,
			EditorPrimitiveCollider3DTrigger,
			EditorPrimitiveMaterialCount
		};

		const float4 primitive_material_colors[EditorPrimitiveMaterialCount] = {
			theme::ddgi_volume_color,
			theme::ddgi_probe_color,
			theme::ddgi_probe_relocated_color,
			theme::ddgi_probe_invalid_color,
			theme::cpu_bvh_internal_color,
			theme::cpu_bvh_leaf_color,
			theme::gpu_bvh_internal_color,
			theme::gpu_bvh_leaf_color,
			theme::collider_3d_color,
			theme::collider_3d_trigger_color
		};

		if (auto material = editor_viewport.view->scene->GetComponent<ecs::MaterialComponent>(editor_viewport.debug_primitive_entity))
		{
			material->SetMaterial(std::make_shared<resource::Material>());
			for (const float4& color : primitive_material_colors)
			{
				auto& material_slot = material->AddMaterialSlot();
				material_slot.base_color = color;
				material_slot.metallic = 0.0f;
				material_slot.roughness = 1.0f;
			}
		}

		editor_viewport.debug_primitive_mesh->positions.clear();
		editor_viewport.debug_primitive_mesh->indices.clear();
		editor_viewport.debug_primitive_mesh->submeshes.clear();

		struct PrimitiveBatch
		{
			uint32 first_index = 0;
			uint32 index_count = 0;
			uint32 material_slot = 0;
		};

		Vector<PrimitiveBatch> primitive_batches;

		auto add_line = [&](const float3& from, const float3& to, uint32 material_slot)
		{
			const uint32 first_vertex = static_cast<uint32>(editor_viewport.debug_primitive_mesh->positions.size());
			const uint32 first_index = static_cast<uint32>(editor_viewport.debug_primitive_mesh->indices.size());
			editor_viewport.debug_primitive_mesh->positions.push_back(from);
			editor_viewport.debug_primitive_mesh->positions.push_back(to);
			editor_viewport.debug_primitive_mesh->indices.push_back(first_vertex);
			editor_viewport.debug_primitive_mesh->indices.push_back(first_vertex + 1);
			if (!primitive_batches.empty() && primitive_batches.back().material_slot == material_slot && primitive_batches.back().first_index + primitive_batches.back().index_count == first_index)
			{
				primitive_batches.back().index_count += 2;
			}
			else
			{
				primitive_batches.push_back({ first_index, 2, material_slot });
			}
		};

		auto add_box = [&](const float3& bounds_min, const float3& bounds_max, uint32 material_slot)
		{
			const float3 corners[8] = {
				{ bounds_min.x, bounds_min.y, bounds_min.z },
				{ bounds_max.x, bounds_min.y, bounds_min.z },
				{ bounds_max.x, bounds_max.y, bounds_min.z },
				{ bounds_min.x, bounds_max.y, bounds_min.z },
				{ bounds_min.x, bounds_min.y, bounds_max.z },
				{ bounds_max.x, bounds_min.y, bounds_max.z },
				{ bounds_max.x, bounds_max.y, bounds_max.z },
				{ bounds_min.x, bounds_max.y, bounds_max.z }
			};
			const uint32 edges[12][2] = {
				{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
				{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
				{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
			};
			for (const auto& edge : edges)
			{
				add_line(corners[edge[0]], corners[edge[1]], material_slot);
			}
		};

		auto add_point = [&](const float3& position, float size, uint32 material_slot)
		{
			add_line({ position.x - size, position.y, position.z }, { position.x + size, position.y, position.z }, material_slot);
			add_line({ position.x, position.y - size, position.z }, { position.x, position.y + size, position.z }, material_slot);
			add_line({ position.x, position.y, position.z - size }, { position.x, position.y, position.z + size }, material_slot);
		};

		auto add_sphere = [&](const float3& center, float radius, uint32 material_slot)
		{
			if (radius <= 0.0f)
			{
				return;
			}

			constexpr uint32 segment_count = 32;
			float3 previous_xy = { center.x + radius, center.y, center.z };
			float3 previous_xz = { center.x + radius, center.y, center.z };
			float3 previous_yz = { center.x, center.y + radius, center.z };
			for (uint32 segment_index = 1; segment_index <= segment_count; ++segment_index)
			{
				const float angle = math::PI * 2.0f * static_cast<float>(segment_index) / static_cast<float>(segment_count);
				const float cos_angle = std::cos(angle);
				const float sin_angle = std::sin(angle);
				const float3 current_xy = { center.x + cos_angle * radius, center.y + sin_angle * radius, center.z };
				const float3 current_xz = { center.x + cos_angle * radius, center.y, center.z + sin_angle * radius };
				const float3 current_yz = { center.x, center.y + cos_angle * radius, center.z + sin_angle * radius };
				add_line(previous_xy, current_xy, material_slot);
				add_line(previous_xz, current_xz, material_slot);
				add_line(previous_yz, current_yz, material_slot);
				previous_xy = current_xy;
				previous_xz = current_xz;
				previous_yz = current_yz;
			}
		};

		if (editor_viewport.debug_settings.show_colliders)
		{
			auto collider_3d_array = editor_viewport.view->scene->GetComponentArray<ecs::Collider3DComponent>().get();
			auto transform_array = editor_viewport.view->scene->GetComponentArray<ecs::TransformComponent>().get();
			if (collider_3d_array && transform_array)
			{
				for (Size collider_index = 0; collider_index < collider_3d_array->GetSize(); ++collider_index)
				{
					const ecs::Entity entity = collider_3d_array->index_to_entity[collider_index];
					if (!transform_array->HasData(entity))
					{
						continue;
					}

					const ecs::Collider3DComponent& collider = collider_3d_array->data[collider_index];
					if (!collider.IsEnabled())
					{
						continue;
					}

					const ecs::TransformComponent& transform = transform_array->GetData(entity);
					const XMMATRIX world = transform.GetWorldTransform();
					const uint32 material_slot = collider.IsTrigger() ? EditorPrimitiveCollider3DTrigger : EditorPrimitiveCollider3D;
					if (collider.shape_type == ecs::Collider3DComponent::ShapeType::Sphere)
					{
						const XMVECTOR center = XMVector3TransformCoord(XMLoadFloat3(&collider.offset), world);
						const float scale_x = XMVectorGetX(XMVector3Length(world.r[0]));
						const float scale_y = XMVectorGetX(XMVector3Length(world.r[1]));
						const float scale_z = XMVectorGetX(XMVector3Length(world.r[2]));
						const float max_scale = (std::max)((std::max)(scale_x, scale_y), scale_z);
						float3 world_center = {};
						XMStoreFloat3(&world_center, center);
						add_sphere(world_center, (std::max)(0.0f, collider.radius) * max_scale, material_slot);
					}
					else
					{
						math::AABB local_bounds = {};
						const float3 half_extent = {
							(std::max)(0.0f, collider.half_extent.x),
							(std::max)(0.0f, collider.half_extent.y),
							(std::max)(0.0f, collider.half_extent.z)
						};
						local_bounds.CreateFromHalfWidth(collider.offset, half_extent);
						const math::AABB world_bounds = local_bounds.TransformAABB(world);
						if (world_bounds.IsValid())
						{
							add_box(world_bounds.min, world_bounds.max, material_slot);
						}
					}
				}
			}
		}

		if (editor_viewport.debug_settings.show_ddgi_overlay && renderer)
		{
			const rendering::RendererDebugDDGIState& ddgi_state = editor_viewport.renderer_debug_state.ddgi;
			if (ddgi_state.volume_active)
			{
				if (editor_viewport.debug_settings.show_ddgi_volume)
				{
					add_box(ddgi_state.volume_min, ddgi_state.volume_max, EditorPrimitiveDDGIVolume);
				}

				if (editor_viewport.debug_settings.show_ddgi_probes && ddgi_state.total_probe_count > 0)
				{
					const float min_probe_spacing = (std::min)(ddgi_state.probe_spacing.x, (std::min)(ddgi_state.probe_spacing.y, ddgi_state.probe_spacing.z));
					const float point_size = (std::max)(0.04f, min_probe_spacing * 0.08f);
					uint32 drawn_probe_count = 0;
					const uint32 max_probe_draw_count = static_cast<uint32>((std::max)(1, editor_viewport.debug_settings.ddgi_max_probe_draw_count));
					if (!ddgi_state.probes.empty())
					{
						for (const rendering::RendererDebugDDGIState::DDGIProbe& probe : ddgi_state.probes)
						{
							if (drawn_probe_count >= max_probe_draw_count)
							{
								break;
							}
							const uint32 probe_material_slot = probe.validity <= 0.0f ? EditorPrimitiveDDGIProbeInvalid : (probe.relocation > 0.0f ? EditorPrimitiveDDGIProbeRelocated : EditorPrimitiveDDGIProbe);
							add_point(probe.position, point_size, probe_material_slot);
							++drawn_probe_count;
						}
					}
					else
					{
						const float sample_ratio = static_cast<float>(ddgi_state.total_probe_count) / static_cast<float>(max_probe_draw_count);
						const uint32 sampling_step = sample_ratio > 1.0f ? static_cast<uint32>((std::max)(1, static_cast<int>(std::ceil(std::cbrt(sample_ratio))))) : 1u;
						for (uint32 z = 0; z < ddgi_state.probe_counts.z; z += sampling_step)
						{
							for (uint32 y = 0; y < ddgi_state.probe_counts.y; y += sampling_step)
							{
								for (uint32 x = 0; x < ddgi_state.probe_counts.x; x += sampling_step)
								{
									if (drawn_probe_count >= max_probe_draw_count)
									{
										break;
									}
									const float3 probe_position = {
										ddgi_state.volume_min.x + static_cast<float>(x) * ddgi_state.probe_spacing.x,
										ddgi_state.volume_min.y + static_cast<float>(y) * ddgi_state.probe_spacing.y,
										ddgi_state.volume_min.z + static_cast<float>(z) * ddgi_state.probe_spacing.z
									};
									add_point(probe_position, point_size, EditorPrimitiveDDGIProbe);
									++drawn_probe_count;
								}
							}
						}
					}
				}
			}
		}

		if (editor_viewport.debug_settings.show_bvh_debug && renderer)
		{
			const rendering::RendererDebugBVHState& bvh_state = editor_viewport.renderer_debug_state.bvh;
			if (editor_viewport.debug_settings.show_cpu_bvh_nodes && bvh_state.cpu_bvh_available)
			{
				for (const rendering::RendererDebugBVHState::BVHNode& node : bvh_state.cpu_nodes)
				{
					add_box(node.bounds_min, node.bounds_max, node.is_leaf ? EditorPrimitiveCPUBVHLeaf : EditorPrimitiveCPUBVHInternal);
				}
			}
			if (editor_viewport.debug_settings.show_gpu_bvh_nodes && bvh_state.gpu_bvh_available)
			{
				for (const rendering::RendererDebugBVHState::BVHNode& node : bvh_state.gpu_nodes)
				{
					add_box(node.bounds_min, node.bounds_max, node.is_leaf ? EditorPrimitiveGPUBVHLeaf : EditorPrimitiveGPUBVHInternal);
				}
			}
		}

		if (editor_viewport.debug_primitive_mesh->render_data.IsValid())
		{
			if (editor_viewport.debug_primitive_mesh->render_data.buffer)
			{
				EditorViewport::DeferredResRemoval deferred_res_removal = {};
				deferred_res_removal.frames_left = 8;
				deferred_res_removal.resources.push_back(editor_viewport.debug_primitive_mesh->render_data.buffer);
				editor_viewport.deferred_res_removals.push_back(std::move(deferred_res_removal));
			}
		}

		editor_viewport.debug_primitive_mesh->ClearRenderData();
		if (!editor_viewport.debug_primitive_mesh->indices.empty())
		{
			auto make_submesh = [&](uint32 first_index, uint32 index_count, resource::PrimitiveTopology topology, uint32 material_slot)
			{
				resource::Submesh submesh = {};
				submesh.first_index = first_index;
				submesh.index_count = index_count;
				submesh.first_vertex = 0;
				submesh.material_slot = material_slot;
				submesh.primitive_topology = topology;
				submesh.local_bounds.Invalidate();
				for (uint32 index = first_index; index < first_index + index_count; ++index)
				{
					const uint32 vertex_index = editor_viewport.debug_primitive_mesh->indices[index];
					if (vertex_index >= editor_viewport.debug_primitive_mesh->positions.size())
					{
						continue;
					}
					math::AABB vertex_bounds = {};
					vertex_bounds.min = editor_viewport.debug_primitive_mesh->positions[vertex_index];
					vertex_bounds.max = editor_viewport.debug_primitive_mesh->positions[vertex_index];
					submesh.local_bounds.Merge(vertex_bounds);
				}
				editor_viewport.debug_primitive_mesh->submeshes.push_back(submesh);
			};

			for (const PrimitiveBatch& batch : primitive_batches)
			{
				if (batch.index_count > 0)
				{
					make_submesh(batch.first_index, batch.index_count, resource::PrimitiveTopology::LineList, batch.material_slot);
				}
			}

			rendering::utils::CreateRenderData(*device, *editor_viewport.debug_primitive_mesh);
		}

		if (auto geometry = editor_viewport.view->scene->GetComponent<ecs::GeometryComponent>(editor_viewport.debug_primitive_entity))
		{
			geometry->UpdateLocalBounds();
			geometry->SetDirty();
		}
	}

	void EditorApplication::LoadEditorSettings()
	{
		EditorSettings loaded_settings = {};
		LoadSettings(editor_settings.settings_path, loaded_settings);
		loaded_settings.settings_path = editor_settings.settings_path;
		editor_settings = loaded_settings;

		content_browser.current_folder = editor_settings.content_current_folder;
		content_browser.type_filter = static_cast<ContentAssetType>(editor_settings.content_type_filter);
		content_browser.tile_size = (std::max)(48.0f, (std::min)(128.0f, editor_settings.content_tile_size));
		editor_viewport.debug_settings.show_grid = editor_settings.viewport_show_grid;
		editor_viewport.debug_settings.show_colliders = editor_settings.viewport_show_colliders;
		editor_viewport.debug_settings.use_wireframe = editor_settings.viewport_use_wireframe;
		editor_viewport.debug_settings.show_bvh_debug = editor_settings.viewport_show_bvh_debug;
		editor_viewport.debug_settings.show_cpu_bvh_nodes = editor_settings.viewport_show_cpu_bvh_nodes;
		editor_viewport.debug_settings.show_gpu_bvh_nodes = editor_settings.viewport_show_gpu_bvh_nodes;
		editor_viewport.debug_settings.show_ddgi_overlay = editor_settings.viewport_show_ddgi_overlay;
		editor_viewport.debug_settings.show_ddgi_volume = editor_settings.viewport_show_ddgi_volume;
		editor_viewport.debug_settings.show_ddgi_probes = editor_settings.viewport_show_ddgi_probes;
		editor_viewport.debug_settings.show_ddgi_text = editor_settings.viewport_show_ddgi_text;
		editor_viewport.debug_settings.show_renderer_stats = editor_settings.viewport_show_renderer_stats;
		editor_viewport.debug_settings.ddgi_max_probe_draw_count = (std::max)(1, editor_settings.viewport_ddgi_max_probe_draw_count);
		editor_camera_speed = (std::max)(0.1f, editor_settings.camera_speed);
	}

	void EditorApplication::SaveEditorSettings()
	{
		editor_settings.content_current_folder = content_browser.current_folder;
		editor_settings.content_type_filter = static_cast<int>(content_browser.type_filter);
		editor_settings.content_tile_size = content_browser.tile_size;
		editor_settings.viewport_show_grid = editor_viewport.debug_settings.show_grid;
		editor_settings.viewport_show_colliders = editor_viewport.debug_settings.show_colliders;
		editor_settings.viewport_use_wireframe = editor_viewport.debug_settings.use_wireframe;
		editor_settings.viewport_show_bvh_debug = editor_viewport.debug_settings.show_bvh_debug;
		editor_settings.viewport_show_cpu_bvh_nodes = editor_viewport.debug_settings.show_cpu_bvh_nodes;
		editor_settings.viewport_show_gpu_bvh_nodes = editor_viewport.debug_settings.show_gpu_bvh_nodes;
		editor_settings.viewport_show_ddgi_overlay = editor_viewport.debug_settings.show_ddgi_overlay;
		editor_settings.viewport_show_ddgi_volume = editor_viewport.debug_settings.show_ddgi_volume;
		editor_settings.viewport_show_ddgi_probes = editor_viewport.debug_settings.show_ddgi_probes;
		editor_settings.viewport_show_ddgi_text = editor_viewport.debug_settings.show_ddgi_text;
		editor_settings.viewport_show_renderer_stats = editor_viewport.debug_settings.show_renderer_stats;
		editor_settings.viewport_ddgi_max_probe_draw_count = editor_viewport.debug_settings.ddgi_max_probe_draw_count;
		editor_settings.camera_speed = editor_camera_speed;
		if (!current_scene_path.empty())
		{
			editor_settings.last_scene_path = io::GetRelativePath(contents_root_dir, current_scene_path);
		}

		SaveSettings(editor_settings.settings_path, editor_settings);
	}

	void EditorApplication::OnWindowResized(int width, int height)
	{
		auto* camera = editor_viewport.view->scene->GetComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity);
		if (!camera)
		{
			return;
		}

		camera->SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
	}

	void EditorApplication::RebuildContentBrowser()
	{
		content_browser.assets.clear();
		content_browser.folders.clear();
		content_browser.folders.push_back("/Contents");
		content_browser.initialized = true;

		auto guess_type = [](const String& extension) -> ContentAssetType
		{
			const String ext = won::utils::ToLower(extension);
			if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp" || ext == resource::texture_binary_extension) return ContentAssetType::Texture;
			if (ext == "mat") return ContentAssetType::Material;
			if (ext == "fbx" || ext == "obj" || ext == "gltf" || ext == "glb" || ext == "stl" || ext == resource::mesh_binary_extension) return ContentAssetType::Mesh;
			if (ext == resource::scene_file_extension) return ContentAssetType::Scene;
			if (ext == "hlsl" || ext == "hlsli") return ContentAssetType::Shader;
			if (ext == "ttf" || ext == "otf") return ContentAssetType::Font;
			if (ext == "lua") return ContentAssetType::Script;
			return ContentAssetType::Unknown;
		};

		Vector<io::DirectoryEntry> entries;
		if (!io::EnumerateDirectoryRecursive(contents_root_dir, &entries))
		{
			content_browser.current_folder = "/Contents";
			return;
		}

		for (const io::DirectoryEntry& entry : entries)
		{
			String relative = io::GetRelativePath(contents_root_dir, entry.path);
			if (relative.empty())
			{
				continue;
			}

			String virtual_path = "/Contents/" + relative;
			if (entry.is_directory)
			{
				content_browser.folders.push_back(virtual_path);
				continue;
			}
			if (!entry.is_file)
			{
				continue;
			}
			String extension = io::GetExtension(entry.path);
			if (won::utils::ToLower(extension) == resource::asset_metadata_extension)
			{
				continue;
			}

			ContentBrowserAsset asset = {};
			asset.disk_path = entry.path;
			asset.virtual_path = virtual_path;
			asset.name = io::GetFilename(entry.path);
			if (!extension.empty() && asset.name.size() > extension.size() + 1)
			{
				asset.name.resize(asset.name.size() - extension.size() - 1);
			}
			asset.type = guess_type(extension);
			asset.id = won::utils::Hash(asset.virtual_path);
			content_browser.assets.push_back(asset);
		}

		std::sort(content_browser.folders.begin(), content_browser.folders.end());
		content_browser.folders.erase(std::unique(content_browser.folders.begin(), content_browser.folders.end()), content_browser.folders.end());
		std::sort(content_browser.assets.begin(), content_browser.assets.end(), [](const ContentBrowserAsset& lhs, const ContentBrowserAsset& rhs)
		{
			return lhs.virtual_path < rhs.virtual_path;
		});

		if (std::find(content_browser.folders.begin(), content_browser.folders.end(), content_browser.current_folder) == content_browser.folders.end())
		{
			content_browser.current_folder = "/Contents";
		}
	}

	void EditorApplication::DrawContentFolderNode(const String& virtual_path, const String& name)
	{
		Vector<String> child_folders;
		const String prefix = virtual_path == "/Contents" ? "/Contents/" : virtual_path + "/";
		for (const String& folder : content_browser.folders)
		{
			if (folder == virtual_path || !won::utils::StartsWith(folder, prefix))
			{
				continue;
			}

			const String rest = folder.substr(prefix.size());
			if (!rest.empty() && rest.find('/') == String::npos)
			{
				child_folders.push_back(folder);
			}
		}

		ImGui::PushID(virtual_path.c_str());
		ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
		if (content_browser.current_folder == virtual_path)
		{
			flags |= ImGuiTreeNodeFlags_Selected;
		}
		if (child_folders.empty())
		{
			flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
		}
		if (virtual_path == "/Contents")
		{
			ImGui::SetNextItemOpen(true, ImGuiCond_Once);
		}

		const String label = String(child_folders.empty() ? ICON_MD_FOLDER " " : ICON_MD_FOLDER_OPEN " ") + name;
		const bool open_node = ImGui::TreeNodeEx(label.c_str(), flags);
		if (ImGui::IsItemClicked())
		{
			content_browser.current_folder = virtual_path;
		}
		if (!child_folders.empty() && open_node)
		{
			for (const String& child_path : child_folders)
			{
				Size slash_pos = child_path.find_last_of('/');
				DrawContentFolderNode(child_path, slash_pos == String::npos ? child_path : child_path.substr(slash_pos + 1));
			}
			ImGui::TreePop();
		}
		ImGui::PopID();
	}

	void EditorApplication::DrawContentAssetTile(const ContentBrowserAsset& asset, float tile_size)
	{
		auto type_name = [](ContentAssetType type) -> const char*
		{
			switch (type)
			{
			case ContentAssetType::Texture: return editor_text::asset_texture;
			case ContentAssetType::Material: return editor_text::asset_material;
			case ContentAssetType::Mesh: return editor_text::asset_mesh;
			case ContentAssetType::Scene: return editor_text::asset_scene;
			case ContentAssetType::Shader: return editor_text::asset_shader;
			case ContentAssetType::Font: return editor_text::asset_font;
			case ContentAssetType::Script: return editor_text::asset_script;
			case ContentAssetType::Unknown: return editor_text::unknown;
			default: return editor_text::asset;
			}
		};
		auto type_icon = [](ContentAssetType type) -> const char*
		{
			switch (type)
			{
			case ContentAssetType::Texture: return ICON_MD_IMAGE;
			case ContentAssetType::Material: return ICON_MD_PALETTE;
			case ContentAssetType::Mesh: return ICON_MD_VIEW_IN_AR;
			case ContentAssetType::Scene: return ICON_MD_DATA_OBJECT;
			case ContentAssetType::Shader: return ICON_MD_CODE;
			case ContentAssetType::Font: return ICON_MD_FONT_DOWNLOAD;
			case ContentAssetType::Script: return ICON_MD_DESCRIPTION;
			default: return ICON_MD_INSERT_DRIVE_FILE;
			}
		};

		ImGui::PushID(asset.virtual_path.c_str());
		ImGui::BeginGroup();
		const bool can_import_asset = asset.type == ContentAssetType::Mesh;
		const bool can_load_scene = asset.type == ContentAssetType::Scene;
		ImGui::Button(type_icon(asset.type), ImVec2(tile_size, tile_size));
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("CONTENT_ASSET_PATH", asset.disk_path.c_str(), asset.disk_path.size() + 1);
			ImGui::TextUnformatted(asset.name.c_str());
			ImGui::EndDragDropSource();
		}

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile_size);
		ImGui::TextWrapped("%s", asset.name.c_str());
		ImGui::TextDisabled("%s", type_name(asset.type));
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();

		if (can_import_asset && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			content_browser.pending_import_name = asset.name;
			content_browser.pending_import_virtual_path = asset.virtual_path;
			content_browser.pending_import_disk_path = asset.disk_path;
			content_browser.pending_import_type = asset.type;
			content_browser.open_import_confirm = true;
		}
		else if (can_load_scene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			LoadScene(asset.disk_path);
		}
		if (ImGui::BeginPopupContextItem("ContentAssetContext"))
		{
			if (ImGui::MenuItem(editor_text::import_to_scene, nullptr, false, can_import_asset))
			{
				content_browser.pending_import_name = asset.name;
				content_browser.pending_import_virtual_path = asset.virtual_path;
				content_browser.pending_import_disk_path = asset.disk_path;
				content_browser.pending_import_type = asset.type;
				content_browser.open_import_confirm = true;
			}
			if (ImGui::MenuItem(editor_text::load_scene, nullptr, false, can_load_scene))
			{
				LoadScene(asset.disk_path);
			}
			ImGui::Separator();
			if (ImGui::MenuItem(editor_text::copy_disk_path))
			{
				ImGui::SetClipboardText(asset.disk_path.c_str());
			}
			if (ImGui::MenuItem(editor_text::copy_virtual_path))
			{
				ImGui::SetClipboardText(asset.virtual_path.c_str());
			}
			ImGui::EndPopup();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(asset.virtual_path.c_str());
			ImGui::TextDisabled("%s", asset.disk_path.c_str());
			ImGui::EndTooltip();
		}
		ImGui::PopID();
	}

	void EditorApplication::DrawContentsBrowser()
	{
		if (!content_browser.initialized)
		{
			RebuildContentBrowser();
		}

		auto type_name = [](ContentAssetType type) -> const char*
		{
			switch (type)
			{
			case ContentAssetType::All: return editor_text::all_types;
			case ContentAssetType::Texture: return editor_text::asset_texture;
			case ContentAssetType::Material: return editor_text::asset_material;
			case ContentAssetType::Mesh: return editor_text::asset_mesh;
			case ContentAssetType::Scene: return editor_text::asset_scene;
			case ContentAssetType::Shader: return editor_text::asset_shader;
			case ContentAssetType::Font: return editor_text::asset_font;
			case ContentAssetType::Script: return editor_text::asset_script;
			case ContentAssetType::Unknown: return editor_text::unknown;
			default: return editor_text::all_types;
			}
		};

		ImGui::TextUnformatted(editor_text::path_label);
		ImGui::SameLine();
		String current_folder = content_browser.current_folder;
		Vector<String> breadcrumb_parts;
		breadcrumb_parts.push_back(editor_text::contents);
		if (current_folder != "/Contents" && won::utils::StartsWith(current_folder, "/Contents/"))
		{
			String rest = current_folder.substr(10);
			Size start = 0;
			while (start < rest.size())
			{
				Size slash_pos = rest.find('/', start);
				if (slash_pos == String::npos)
				{
					breadcrumb_parts.push_back(rest.substr(start));
					break;
				}
				breadcrumb_parts.push_back(rest.substr(start, slash_pos - start));
				start = slash_pos + 1;
			}
		}

		String breadcrumb_path = "/Contents";
		for (Size i = 0; i < breadcrumb_parts.size(); ++i)
		{
			if (i > 0)
			{
				breadcrumb_path += "/" + breadcrumb_parts[i];
				ImGui::SameLine();
				ImGui::TextUnformatted(ICON_MD_CHEVRON_RIGHT);
			}
			ImGui::SameLine();
			if (ImGui::SmallButton(breadcrumb_parts[i].c_str()))
			{
				content_browser.current_folder = breadcrumb_path;
			}
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(220.0f);
		ImGui::InputTextWithHint("##content_search", editor_text::search_hint, content_browser.search, arraysize(content_browser.search));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::BeginCombo("##content_type_filter", type_name(content_browser.type_filter)))
		{
			const ContentAssetType filters[] = { ContentAssetType::All, ContentAssetType::Texture, ContentAssetType::Material, ContentAssetType::Mesh, ContentAssetType::Scene, ContentAssetType::Shader, ContentAssetType::Font, ContentAssetType::Script, ContentAssetType::Unknown };
			for (ContentAssetType filter : filters)
			{
				if (ImGui::Selectable(type_name(filter), content_browser.type_filter == filter))
				{
					content_browser.type_filter = filter;
				}
			}
			ImGui::EndCombo();
		}

		ImGui::SameLine();
		ImGui::SetNextItemWidth(120.0f);
		ImGui::SliderFloat("##content_tile_size", &content_browser.tile_size, 48.0f, 128.0f, editor_text::size_format);

		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_REFRESH))
		{
			RebuildContentBrowser();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(editor_text::refresh);
		}

		ImGui::Separator();
		ImGui::Columns(2, "contents_browser_columns", true);
		ImGui::SetColumnWidth(0, 260.0f);

		ImGui::BeginChild("ContentFolderTree", ImVec2(0.0f, 0.0f), true);
		DrawContentFolderNode("/Contents", "Contents");
		ImGui::EndChild();

		ImGui::NextColumn();
		ImGui::BeginChild("ContentAssetView", ImVec2(0.0f, 0.0f), false);

		Vector<String> child_folders;
		const String prefix = content_browser.current_folder == "/Contents" ? "/Contents/" : content_browser.current_folder + "/";
		for (const String& folder : content_browser.folders)
		{
			if (folder == content_browser.current_folder || !won::utils::StartsWith(folder, prefix))
			{
				continue;
			}
			const String rest = folder.substr(prefix.size());
			if (!rest.empty() && rest.find('/') == String::npos)
			{
				child_folders.push_back(folder);
			}
		}

		const float tile_size = content_browser.tile_size;
		const float cell_size = tile_size + 16.0f;
		const int column_count = (std::max)(1, static_cast<int>(ImGui::GetContentRegionAvail().x / cell_size));
		if (!child_folders.empty())
		{
			ImGui::Columns(column_count, "content_folder_grid", false);
			for (const String& folder_path : child_folders)
			{
				Size slash_pos = folder_path.find_last_of('/');
				String folder_name = slash_pos == String::npos ? folder_path : folder_path.substr(slash_pos + 1);
				ImGui::PushID(folder_path.c_str());
				ImGui::BeginGroup();
				ImGui::Button(ICON_MD_FOLDER, ImVec2(tile_size, tile_size));
				if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
				{
					content_browser.current_folder = folder_path;
				}
				ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile_size);
				ImGui::TextWrapped("%s", folder_name.c_str());
				ImGui::TextDisabled(editor_text::folder);
				ImGui::PopTextWrapPos();
				ImGui::EndGroup();
				ImGui::PopID();
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
			ImGui::Separator();
		}

		Vector<const ContentBrowserAsset*> filtered_assets;
		const String search_lower = won::utils::ToLower(content_browser.search);
		for (const ContentBrowserAsset& asset : content_browser.assets)
		{
			if (!won::utils::StartsWith(asset.virtual_path, prefix))
			{
				continue;
			}
			String rest = asset.virtual_path.substr(prefix.size());
			if (rest.find('/') != String::npos)
			{
				continue;
			}
			if (content_browser.type_filter != ContentAssetType::All && asset.type != content_browser.type_filter)
			{
				continue;
			}
			if (!search_lower.empty())
			{
				String name_lower = won::utils::ToLower(asset.name);
				String path_lower = won::utils::ToLower(asset.virtual_path);
				if (name_lower.find(search_lower) == String::npos && path_lower.find(search_lower) == String::npos)
				{
					continue;
				}
			}
			filtered_assets.push_back(&asset);
		}

		if (filtered_assets.empty() && child_folders.empty())
		{
			ImGui::TextDisabled(editor_text::folder_empty);
		}
		else
		{
			ImGui::Columns(column_count, "content_asset_grid", false);
			for (const ContentBrowserAsset* asset : filtered_assets)
			{
				DrawContentAssetTile(*asset, tile_size);
				ImGui::NextColumn();
			}
			ImGui::Columns(1);
		}

		ImGui::EndChild();
		ImGui::Columns(1);

		if (content_browser.open_import_confirm)
		{
			ImGui::OpenPopup(editor_text::import_content_asset_popup);
			content_browser.open_import_confirm = false;
		}

		if (ImGui::BeginPopup(editor_text::import_content_asset_popup, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted(editor_text::import_content_asset_message);
			ImGui::TextUnformatted(content_browser.pending_import_name.c_str());
			ImGui::TextDisabled("%s", content_browser.pending_import_virtual_path.c_str());
			const bool can_import = content_browser.pending_import_type == ContentAssetType::Mesh && !content_browser.pending_import_disk_path.empty();
			if (!can_import)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button(editor_text::import))
			{
				StartAssetImport(content_browser.pending_import_disk_path);
				content_browser.pending_import_name.clear();
				content_browser.pending_import_virtual_path.clear();
				content_browser.pending_import_disk_path.clear();
				content_browser.pending_import_type = ContentAssetType::Unknown;
				ImGui::CloseCurrentPopup();
			}
			if (!can_import)
			{
				ImGui::EndDisabled();
			}
			ImGui::SameLine();
			if (ImGui::Button(editor_text::cancel))
			{
				content_browser.pending_import_name.clear();
				content_browser.pending_import_virtual_path.clear();
				content_browser.pending_import_disk_path.clear();
				content_browser.pending_import_type = ContentAssetType::Unknown;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}
	}

	void EditorApplication::DrawEditorGrid()
	{
		if (!editor_viewport.debug_settings.show_grid || !editor_grid_pso || !renderer)
		{
			return;
		}

		const ecs::CameraComponent* camera = editor_viewport.view->scene->GetComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity);
		if (!camera)
		{
			return;
		}

		struct EditorGridConstants
		{
			float4x4 view_projection = {};
			float4x4 inv_view_projection = {};
			float4 camera_position = {};
			float4 grid_color = {};
			float4 axis_x_color = {};
			float4 axis_z_color = {};
		};

		EditorGridConstants constants = {};
		constants.view_projection = camera->view_projection;
		constants.inv_view_projection = camera->inv_view_projection;
		constants.camera_position = { camera->eye.x, camera->eye.y, camera->eye.z, 1.0f };
		constants.grid_color = theme::editor_grid_color;
		constants.axis_x_color = theme::editor_grid_axis_x_color;
		constants.axis_z_color = theme::editor_grid_axis_z_color;

		Renderer::FrameContext& frame_context = renderer->GetFrameContext();
		RHICommandList* command_list = frame_context.BeginCommandList(*device);
		if (!command_list)
		{
			return;
		}

		jobsystem::Execute(renderer->GetRenderingWorkContext(), [this, command_list, constants](jobsystem::JobArgs args) {
			Renderer::FrameContext& frame_context = renderer->GetFrameContext();

			RHISubresourceBinding back_buffer_binding = {};
			RHISubresourceBinding depth_buffer_binding = {};
			if (!renderer->GetCurrentBackBufferBinding(back_buffer_binding) ||
				!renderer->GetCurrentDepthBufferBinding(depth_buffer_binding))
			{
				return;
			}

			RHIBufferDesc buffer_desc = {};
			buffer_desc.bind_flags = RHIBindFlags::ConstantBuffer;
			Renderer::FrameUploadAllocation allocation = {};
			if (!frame_context.AllocateFrameUpload(*device, sizeof(EditorGridConstants), device->GetMinOffsetAlignment(buffer_desc), allocation))
			{
				return;
			}
			std::memcpy(allocation.mapped_data, &constants, sizeof(EditorGridConstants));

			RHISubresourceHandle constants_subresource = {};
			RHISubresourceDesc subresource_desc = {};
			subresource_desc.type = RHISubresourceType::ConstantBuffer;
			subresource_desc.buffer_offset = allocation.buffer_offset;
			subresource_desc.buffer_size = sizeof(EditorGridConstants);
			subresource_desc.buffer_stride = sizeof(EditorGridConstants);
			if (!device->CreateSubresource(*allocation.buffer, subresource_desc, &constants_subresource))
			{
				return;
			}

			RHISubresourceBinding constants_binding = {};
			constants_binding.resource = allocation.buffer.get();
			constants_binding.subresource = constants_subresource;

			RHIViewport viewport = {};
			viewport.x = static_cast<float>(editor_viewport.view->viewport.x);
			viewport.y = static_cast<float>(editor_viewport.view->viewport.y);
			viewport.width = static_cast<float>(editor_viewport.view->viewport.width);
			viewport.height = static_cast<float>(editor_viewport.view->viewport.height);
			viewport.min_depth = 0.0f;
			viewport.max_depth = 1.0f;

			RHIRect scissor = {};
			scissor.x = editor_viewport.view->scissor.x;
			scissor.y = editor_viewport.view->scissor.y;
			scissor.width = editor_viewport.view->scissor.width;
			scissor.height = editor_viewport.view->scissor.height;

			Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };
			auto gpu_range = profiler::ScopedRangeGPU("Editor Grid Pass", *command_list);
			command_list->BeginEvent("Editor Grid Pass");
			command_list->TransitionResource(*back_buffer_binding.resource, RHIResourceState::RenderTarget);
			command_list->TransitionResource(*depth_buffer_binding.resource, RHIResourceState::DepthWrite);
			command_list->SetRenderTargets(color_targets, &depth_buffer_binding);
			command_list->SetViewport(viewport);
			command_list->SetScissor(scissor);
			command_list->SetGraphicsPipeline(*editor_grid_pso);
			command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, constants_binding);
			command_list->SetConstantBuffer(RHIShaderStage::Pixel, 0, constants_binding);
			command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
			command_list->Draw(3, 1, 0, 0);
			command_list->EndEvent();
		});
	}

	void EditorApplication::RenderUI()
	{
#ifdef _WIN32
		ImGui_ImplWin32_NewFrame();
#endif
		ImGui::NewFrame();
		//ImGui::ShowDemoWindow();

		ImGuiIO& io = ImGui::GetIO();

		ImGuiWindowFlags flags =
			ImGuiWindowFlags_MenuBar |
			ImGuiWindowFlags_NoDocking |
			ImGuiWindowFlags_NoTitleBar |
			ImGuiWindowFlags_NoCollapse |
			ImGuiWindowFlags_NoResize |
			ImGuiWindowFlags_NoMove |
			ImGuiWindowFlags_NoBringToFrontOnFocus |
			ImGuiWindowFlags_NoNavFocus;

		ImGui::SetNextWindowPos(ImVec2(0, 0), ImGuiCond_Always);
		ImGui::SetNextWindowSize(io.DisplaySize, ImGuiCond_Always);

		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0, 0, 0, 0));
		ImGui::Begin(editor_text::main_window, NULL, flags);

		ImGui::PopStyleVar(3);

		ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

		if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
			BuildDefaultDockLayout(dockspace_id, io.DisplaySize);

		static bool open_save_scene_as = false;
		static bool open_load_scene = false;
		static bool open_new_scene = false;
		static bool open_load_project = false;
		static bool open_new_project = false;
		static bool open_save_material_asset = false;
		static bool save_as_then_load = false;
		static bool save_as_then_new_scene = false;
		static bool save_as_then_load_project = false;
		static bool save_as_then_new_project = false;
		static bool save_current_then_new_scene = false;
		static bool save_current_then_load_project = false;
		static bool save_current_then_new_project = false;
		static bool focus_contents_browser_on_startup = true;
		bool open_save_current_scene = false;

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu(editor_text::file_menu))
			{
				if (ImGui::MenuItem(editor_text::new_project))
				{
					save_as_then_load = false;
					save_as_then_new_scene = false;
					save_as_then_load_project = false;
					save_as_then_new_project = false;
					save_current_then_new_scene = false;
					save_current_then_load_project = false;
					save_current_then_new_project = true;
					open_save_current_scene = true;
				}
				if (ImGui::MenuItem(editor_text::load_project))
				{
					save_as_then_load = false;
					save_as_then_new_scene = false;
					save_as_then_load_project = false;
					save_as_then_new_project = false;
					save_current_then_new_scene = false;
					save_current_then_load_project = true;
					save_current_then_new_project = false;
					open_save_current_scene = true;
				}
				if (ImGui::MenuItem(editor_text::save_project, nullptr, false, !loaded_project_settings.settings_path.empty()))
				{
					SaveProject();
				}
				if (ImGui::MenuItem(editor_text::project_settings))
				{
					show_project_settings_window = true;
				}
					ImGui::TextDisabled("%s", loaded_project_settings.settings_path.empty() ? editor_text::no_project : loaded_project_settings.settings_path.c_str());
				ImGui::Separator();
				if (ImGui::MenuItem(editor_text::new_scene))
				{
					save_as_then_load = false;
					save_as_then_new_scene = false;
					save_as_then_load_project = false;
					save_as_then_new_project = false;
					save_current_then_new_scene = true;
					save_current_then_load_project = false;
					save_current_then_new_project = false;
					open_save_current_scene = true;
				}
				if (ImGui::MenuItem(editor_text::load_scene))
				{
					save_as_then_load = false;
					save_as_then_new_scene = false;
					save_as_then_load_project = false;
					save_as_then_new_project = false;
					save_current_then_new_scene = false;
					save_current_then_load_project = false;
					save_current_then_new_project = false;
					open_save_current_scene = true;
				}
				if (ImGui::MenuItem(editor_text::save_scene))
				{
					if (current_scene_path.empty())
					{
						save_as_then_load = false;
						save_as_then_new_scene = false;
						save_as_then_load_project = false;
						save_as_then_new_project = false;
						open_save_scene_as = true;
					}
					else
					{
						SaveScene(current_scene_path);
					}
				}
				if (ImGui::MenuItem(editor_text::save_scene_as))
				{
					save_as_then_load = false;
					save_as_then_new_scene = false;
					save_as_then_load_project = false;
					save_as_then_new_project = false;
					open_save_scene_as = true;
				}
				ImGui::TextDisabled("%s", current_scene_path.empty() ? editor_text::unsaved_scene : current_scene_path.c_str());
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(editor_text::build_menu))
			{
				const bool can_package_project = !loaded_project_settings.settings_path.empty();
				if (ImGui::MenuItem(editor_text::package_project, nullptr, false, can_package_project))
				{
					const bool scene_saved = current_scene_path.empty() || SaveScene(current_scene_path);
					if (scene_saved)
					{
						project::SaveSettings(loaded_project_settings.settings_path, loaded_project_settings);

						const String package_tool_path = io::CombinePath(io::GetExecutableDirectory(), "PackageTool.exe");
						if (!io::IsFile(package_tool_path))
						{
							backlog::Post(editor_text::package_tool_not_found + package_tool_path, backlog::LogLevel::Warning);
						}
						else
						{
							const String arguments = "\"" + loaded_project_settings.settings_path + "\" Release";
							HINSTANCE result = ShellExecuteA(static_cast<HWND>(window ? window->GetNativeHandle() : nullptr), "open", package_tool_path.c_str(), arguments.c_str(), io::GetExecutableDirectory().c_str(), SW_SHOWNORMAL);
							backlog::Post(reinterpret_cast<INT_PTR>(result) > 32 ? editor_text::package_project_started + loaded_project_settings.settings_path : editor_text::package_project_failed, reinterpret_cast<INT_PTR>(result) > 32 ? backlog::LogLevel::Default : backlog::LogLevel::Warning);
						}
					}
				}
				if (!can_package_project)
				{
					ImGui::TextDisabled(editor_text::package_project_missing_settings);
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(editor_text::window_menu))
			{
				if (ImGui::MenuItem(editor_text::reset_layout))
				{
					BuildDefaultDockLayout(dockspace_id, io.DisplaySize);
					focus_contents_browser_on_startup = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(editor_text::plugins_menu))
			{
				if (plugins.empty())
				{
					ImGui::TextDisabled(editor_text::no_plugins_found);
				}
				for (Size plugin_index = 0; plugin_index < plugins.size(); ++plugin_index)
				{
					EditorPluginInfo& plugin_info = plugins[plugin_index];
					bool enabled = plugin_info.enabled;
					const bool default_plugin = plugin_info.info.type == plugin::PluginType::EditorDefault;
					const String label = (plugin_info.info.display_name.empty() ? plugin_info.info.plugin_id : plugin_info.info.display_name) + "##" + plugin_info.info.plugin_id;

					if (default_plugin)
					{
						ImGui::BeginDisabled();
					}
					if (ImGui::Checkbox(label.c_str(), &enabled))
					{
						SetPluginEnabled(plugin_index, enabled);
					}
					if (default_plugin)
					{
						ImGui::EndDisabled();
					}

					if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
					{
						const char* type_name = editor_text::unknown;
						if (plugin_info.info.type == plugin::PluginType::EditorDefault) { type_name = "EditorDefault"; }
						else if (plugin_info.info.type == plugin::PluginType::EditorOptional) { type_name = "EditorOptional"; }
						else if (plugin_info.info.type == plugin::PluginType::RuntimeOptional) { type_name = "RuntimeOptional"; }
						ImGui::SetTooltip("%s\n%s\n%s", plugin_info.info.plugin_id.c_str(), type_name, plugin_info.plugin ? editor_text::plugin_loaded : editor_text::plugin_not_loaded);
					}
				}
				ImGui::EndMenu();
			}

#ifdef EDITOR_USE_CUSTOM_TITLEBAR
			const float button_size = ImGui::GetFrameHeight();
			const float button_spacing = ImGui::GetStyle().ItemSpacing.x;
			const float controls_width = button_size * 3.0f + button_spacing * 2.0f;
			const float controls_pos_x = ImGui::GetWindowWidth() - controls_width - ImGui::GetStyle().WindowPadding.x;
			const float drag_width = (std::max)(0.0f, controls_pos_x - ImGui::GetCursorPosX() - button_spacing);
			ImGui::InvisibleButton("TitleBarDragZone", ImVec2(drag_width, button_size));
			static bool title_bar_drag_suppressed = false;
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				title_bar_drag_suppressed = true;
				if (window->IsMaximized())
				{
					window->Restore();
				}
				else
				{
					window->Maximize();
				}
			}
			static bool title_bar_dragging = false;
			static int drag_start_cursor_x = 0;
			static int drag_start_cursor_y = 0;
			static int drag_start_window_x = 0;
			static int drag_start_window_y = 0;
			static float drag_start_cursor_ratio_x = 0.5f;
			if (ImGui::IsItemActivated())
			{
				title_bar_dragging = window->GetCursorPosition(drag_start_cursor_x, drag_start_cursor_y) && window->GetPosition(drag_start_window_x, drag_start_window_y);
				const int drag_start_window_width = (std::max)(1, window->GetWidth());
				drag_start_cursor_ratio_x = static_cast<float>(drag_start_cursor_x - drag_start_window_x) / static_cast<float>(drag_start_window_width);
				drag_start_cursor_ratio_x = (std::max)(0.05f, (std::min)(0.95f, drag_start_cursor_ratio_x));
			}
			if (!title_bar_drag_suppressed && title_bar_dragging && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
			{
				int cursor_x = 0;
				int cursor_y = 0;
				if (window->GetCursorPosition(cursor_x, cursor_y))
				{
					if (window->IsMaximized())
					{
						const int drag_start_cursor_offset_y = drag_start_cursor_y - drag_start_window_y;
						window->Restore();
						const int restored_width = (std::max)(1, window->GetWidth());
						drag_start_window_x = cursor_x - static_cast<int>(static_cast<float>(restored_width) * drag_start_cursor_ratio_x);
						drag_start_window_y = cursor_y - drag_start_cursor_offset_y;
						drag_start_cursor_x = cursor_x;
						drag_start_cursor_y = cursor_y;
					}
					window->SetPosition(drag_start_window_x + cursor_x - drag_start_cursor_x, drag_start_window_y + cursor_y - drag_start_cursor_y);
				}
			}
			if (!ImGui::IsItemActive())
			{
				title_bar_dragging = false;
				title_bar_drag_suppressed = false;
			}
			ImGui::SameLine();
			ImGui::SetCursorPosX(controls_pos_x);
			if (ImGui::Button("\xE2\x88\x92", ImVec2(button_size, button_size)))
			{
				window->Minimize();
			}
			ImGui::SameLine();
			if (ImGui::Button(window->IsMaximized() ? "\xE2\x96\xA3" : "\xE2\x96\xA1", ImVec2(button_size, button_size)))
			{
				if (window->IsMaximized())
				{
					window->Restore();
				}
				else
				{
					window->Maximize();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button("\xC3\x97", ImVec2(button_size, button_size)))
			{
				window->Close();
			}
#endif

			ImGui::EndMenuBar();
		}

		if (open_save_current_scene)
		{
			ImGui::OpenPopup(editor_text::save_current_scene_popup);
		}

		if (ImGui::BeginPopupModal(editor_text::save_current_scene_popup, nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted(editor_text::save_current_scene_message);
			if (ImGui::Button(current_scene_path.empty() ? editor_text::save_as : editor_text::save))
			{
				if (current_scene_path.empty())
				{
					save_as_then_load = !save_current_then_new_scene && !save_current_then_load_project && !save_current_then_new_project;
					save_as_then_new_scene = save_current_then_new_scene;
					save_as_then_load_project = save_current_then_load_project;
					save_as_then_new_project = save_current_then_new_project;
					open_save_scene_as = true;
					ImGui::CloseCurrentPopup();
				}
				else if (SaveScene(current_scene_path))
				{
					if (save_current_then_new_scene)
					{
						open_new_scene = true;
					}
					else if (save_current_then_load_project)
					{
						open_load_project = true;
					}
					else if (save_current_then_new_project)
					{
						open_new_project = true;
					}
					else
					{
						open_load_scene = true;
					}
					save_current_then_new_scene = false;
					save_current_then_load_project = false;
					save_current_then_new_project = false;
					ImGui::CloseCurrentPopup();
				}
			}
			ImGui::SameLine();
			if (ImGui::Button(editor_text::dont_save))
			{
				if (save_current_then_new_scene)
				{
					open_new_scene = true;
				}
				else if (save_current_then_load_project)
				{
					open_load_project = true;
				}
				else if (save_current_then_new_project)
				{
					open_new_project = true;
				}
				else
				{
					open_load_scene = true;
				}
				save_current_then_new_scene = false;
				save_current_then_load_project = false;
				save_current_then_new_project = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button(editor_text::cancel))
			{
				save_current_then_new_scene = false;
				save_current_then_load_project = false;
				save_current_then_new_project = false;
				save_as_then_load = false;
				save_as_then_new_scene = false;
				save_as_then_load_project = false;
				save_as_then_new_project = false;
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (open_save_scene_as)
		{
			open_save_scene_as = false;

			io::FileDialogDesc desc = {};
			desc.owner_window = window ? window->GetNativeHandle() : nullptr;
			desc.title = editor_text::save_scene_as;
			desc.initial_directory = io::CombinePath(contents_root_dir, scene_directory_name);
			desc.default_file_name = String(default_scene_file_name) + "." + resource::scene_file_extension;
			desc.default_extension = resource::scene_file_extension;
			desc.filter_name = editor_text::won_scene_file;
			desc.filter_pattern = String("*.") + resource::scene_file_extension;

			String path;
			if (io::SaveFileDialog(path, desc) && SaveScene(path))
			{
				if (save_as_then_load)
				{
					open_load_scene = true;
				}
				if (save_as_then_new_scene)
				{
					open_new_scene = true;
				}
				if (save_as_then_load_project)
				{
					open_load_project = true;
				}
				if (save_as_then_new_project)
				{
					open_new_project = true;
				}
			}
			save_as_then_load = false;
			save_as_then_new_scene = false;
			save_as_then_load_project = false;
			save_as_then_new_project = false;
		}

		if (open_new_scene)
		{
			open_new_scene = false;
			if (editor_viewport.view && editor_viewport.view->scene)
			{
				editor_viewport.view->scene->ClearEntities();
				current_scene_path.clear();
				editor_viewport.picked_entity = ecs::INVALID_ENTITY;
				editor_viewport.debug_primitive_entity = ecs::INVALID_ENTITY;
				editor_viewport.debug_primitive_mesh.reset();
				CreateStartupScene();
				UpdateEntityList();
			}
		}

		if (open_load_scene)
		{
			open_load_scene = false;

			io::FileDialogDesc desc = {};
			desc.owner_window = window ? window->GetNativeHandle() : nullptr;
			desc.title = editor_text::load_scene;
			desc.initial_directory = io::CombinePath(contents_root_dir, scene_directory_name);
			desc.default_extension = resource::scene_file_extension;
			desc.filter_name = editor_text::won_scene_file;
			desc.filter_pattern = String("*.") + resource::scene_file_extension;

			String path;
			if (io::OpenFileDialog(path, desc))
			{
				LoadScene(path);
			}
		}

		if (open_new_project)
		{
			open_new_project = false;

			io::FileDialogDesc desc = {};
			desc.owner_window = window ? window->GetNativeHandle() : nullptr;
			desc.title = editor_text::new_project;
			const String projects_directory = io::NormalizePath(String(PROJECTS_ROOT_DIR));
			desc.initial_directory = !loaded_project_settings.project_root.empty() ? loaded_project_settings.project_root : io::IsDirectory(projects_directory) ? projects_directory : io::GetExecutableDirectory();
			desc.default_file_name = String("NewProject.") + project::project_file_extension;
			desc.default_extension = project::project_file_extension;
			desc.filter_name = editor_text::won_project_file;
			desc.filter_pattern = String("*.") + project::project_file_extension;

			String path;
			if (io::SaveFileDialog(path, desc))
			{
				NewProject(path);
			}
		}

		if (open_load_project)
		{
			open_load_project = false;

			io::FileDialogDesc desc = {};
			desc.owner_window = window ? window->GetNativeHandle() : nullptr;
			desc.title = editor_text::load_project;
			const String projects_directory = io::NormalizePath(String(PROJECTS_ROOT_DIR));
			desc.initial_directory = !loaded_project_settings.project_root.empty() ? loaded_project_settings.project_root : io::IsDirectory(projects_directory) ? projects_directory : io::GetExecutableDirectory();
			desc.default_extension = project::project_file_extension;
			desc.filter_name = editor_text::won_project_file;
			desc.filter_pattern = String("*.") + project::project_file_extension;

			String path;
			if (io::OpenFileDialog(path, desc))
			{
				LoadProject(path);
			}
		}

		if (open_save_material_asset)
		{
			open_save_material_asset = false;

			MaterialComponent* material_comp = (editor_viewport.view && editor_viewport.view->scene)
				? editor_viewport.view->scene->GetComponent<MaterialComponent>(editor_viewport.picked_entity)
				: nullptr;
			if (material_comp && material_comp->material)
			{
				io::FileDialogDesc desc = {};
				desc.owner_window = window ? window->GetNativeHandle() : nullptr;
				desc.title = editor_text::save_material_asset;
				desc.initial_directory = contents_root_dir;
				desc.default_file_name = String("Material.") + resource::material_binary_extension;
				desc.default_extension = resource::material_binary_extension;
				desc.filter_name = editor_text::won_material_file;
				desc.filter_pattern = String("*.") + resource::material_binary_extension;

				String path;
				if (io::SaveFileDialog(path, desc))
				{
					// Save the file and register this instance as the path's canonical asset (atomic).
					if (resource::SaveMaterialBinary(path, material_comp->material))
					{
						material_comp->SetMaterialAssetPath(io::GetRelativePath(contents_root_dir, path));
						material_comp->SetDirty();
						RebuildContentBrowser();
					}
					else
					{
						backlog::Post(editor_text::save_material_failed + path, backlog::LogLevel::Warning);
					}
				}
			}
		}

		ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

		ImGui::End();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		editor_viewport.input_enabled = false;
		if (ImGui::Begin(editor_text::viewport_window))
		{
			String viewport_title = current_scene_path.empty() ? editor_text::unsaved_scene : io::GetFilename(current_scene_path);
			if (!current_scene_path.empty())
			{
				String extension = io::GetExtension(viewport_title);
				if (!extension.empty() && viewport_title.size() > extension.size() + 1)
				{
					viewport_title.resize(viewport_title.size() - extension.size() - 1);
				}
			}
			ImGui::TextUnformatted(viewport_title.c_str());
			ImGui::SameLine();
			if (ImGui::Button(editor_text::options))
			{
				ImGui::OpenPopup(editor_text::options_popup);
				//ImGui::SetNextWindowSizeConstraints(ImVec2(240, 160), ImVec2(600, 500));
			}

			ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(500, 500));
			if (ImGui::BeginPopup(editor_text::options_popup))
			{
				ImGui::Checkbox(editor_text::wireframe, &editor_viewport.debug_settings.use_wireframe);

				if (window)
				{
					std::shared_ptr<RHISwapchain> swapchain = window->GetRHISwapchain();
					if (swapchain)
					{
						bool vsync_enabled = swapchain->IsVSyncEnabled();
						if (ImGui::Checkbox(editor_text::vsync, &vsync_enabled))
						{
							project_settings.vsync_enabled = vsync_enabled;
							swapchain->SetVSync(vsync_enabled);
						}
					}
				}

				// Live anti-aliasing toggle for quick experimentation (does not persist to the project;
				// the persisted default lives in Project Settings).
				if (editor_viewport.view)
				{
					const char* aa_items[] = { "None", "FXAA" };
					int aa_index = static_cast<int>(editor_viewport.view->options.aa_mode);
					ImGui::SetNextItemWidth(120.0f);
					if (ImGui::Combo(editor_text::anti_aliasing, &aa_index, aa_items, IM_ARRAYSIZE(aa_items)))
					{
						editor_viewport.view->options.aa_mode = static_cast<rendering::AntiAliasingMode>(aa_index);
					}

					const char* tonemap_items[] = { "Reinhard", "ACES" };
					int tonemap_index = static_cast<int>(editor_viewport.view->options.tonemap_mode);
					ImGui::SetNextItemWidth(120.0f);
					if (ImGui::Combo(editor_text::tonemap_mode, &tonemap_index, tonemap_items, IM_ARRAYSIZE(tonemap_items)))
					{
						editor_viewport.view->options.tonemap_mode = static_cast<rendering::TonemapMode>(tonemap_index);
					}
				}

				if (ImGui::Button(editor_text::reset_editor_camera))
				{
					if (editor_viewport.view && editor_viewport.view->scene && editor_viewport.view->camera_entity != ecs::INVALID_ENTITY)
					{
						if (auto transform = editor_viewport.view->scene->GetComponent<ecs::TransformComponent>(editor_viewport.view->camera_entity))
						{
							transform->position = { 0.0f, 0.0f, 0.0f };
							transform->rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
							transform->SetDirty();
						}
						if (auto camera = editor_viewport.view->scene->GetComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity))
						{
							float viewport_width = static_cast<float>(editor_viewport.view->viewport.width);
							float viewport_height = static_cast<float>(editor_viewport.view->viewport.height);
							if (viewport_height <= 0.0f)
							{
								viewport_height = 1.0f;
							}
							camera->SetAspectRatio(viewport_width / viewport_height);
							camera->SetNearFar(0.1f, 1000.0f);
							camera->SetFOV_Y(math::PI / 3.0f);
							camera->SetOrtho(false);
						}
						editor_viewport.camera_controller = {};
					}
				}

				ImGui::Separator();
				ImGui::Checkbox(editor_text::renderer_stats, &editor_viewport.debug_settings.show_renderer_stats);
				ImGui::Checkbox(editor_text::editor_grid, &editor_viewport.debug_settings.show_grid);
				ImGui::Checkbox(editor_text::collider_3d, &editor_viewport.debug_settings.show_colliders);
				ImGui::Separator();
				ImGui::Checkbox(editor_text::bvh_debug, &editor_viewport.debug_settings.show_bvh_debug);
				if (editor_viewport.debug_settings.show_bvh_debug)
				{
					ImGui::Checkbox(editor_text::cpu_bvh_nodes, &editor_viewport.debug_settings.show_cpu_bvh_nodes);
					editor_viewport.debug_settings.show_gpu_bvh_nodes = false;
					ImGui::BeginDisabled();
					ImGui::Checkbox(editor_text::gpu_bvh_nodes, &editor_viewport.debug_settings.show_gpu_bvh_nodes);
					ImGui::EndDisabled();
				}

				ImGui::Separator();
				ImGui::Checkbox(editor_text::ddgi_debug_overlay, &editor_viewport.debug_settings.show_ddgi_overlay);
				if (editor_viewport.debug_settings.show_ddgi_overlay)
				{
					ImGui::Checkbox(editor_text::ddgi_volume, &editor_viewport.debug_settings.show_ddgi_volume);
					ImGui::Checkbox(editor_text::ddgi_probes, &editor_viewport.debug_settings.show_ddgi_probes);
					ImGui::Checkbox(editor_text::ddgi_text, &editor_viewport.debug_settings.show_ddgi_text);
					ImGui::InputInt(editor_text::ddgi_max_probe_draw, &editor_viewport.debug_settings.ddgi_max_probe_draw_count);
					editor_viewport.debug_settings.ddgi_max_probe_draw_count = (std::max)(1, editor_viewport.debug_settings.ddgi_max_probe_draw_count);
				}

				ImGui::Separator();
				if (ImGui::Button(editor_text::close)) ImGui::CloseCurrentPopup();

				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();

			ImVec2 viewport_region_min = ImGui::GetWindowContentRegionMin();
			ImVec2 viewport_region_max = ImGui::GetWindowContentRegionMax();
			ImVec2 window_pos = ImGui::GetWindowPos();
			ImVec2 viewport_pos = ImVec2(window_pos.x + viewport_region_min.x, window_pos.y + viewport_region_min.y);
			ImVec2 viewport_size = ImVec2(viewport_region_max.x - viewport_region_min.x, viewport_region_max.y - viewport_region_min.y);
			editor_viewport.input_enabled = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();

			editor_viewport.view->viewport.x = static_cast<uint32>(viewport_pos.x);
			editor_viewport.view->viewport.y = static_cast<uint32>(viewport_pos.y);
			editor_viewport.view->viewport.width = (std::max)(1u, static_cast<uint32>(viewport_size.x));
			editor_viewport.view->viewport.height = (std::max)(1u, static_cast<uint32>(viewport_size.y));
			editor_viewport.view->scissor.x = editor_viewport.view->viewport.x;
			editor_viewport.view->scissor.y = editor_viewport.view->viewport.y;
			editor_viewport.view->scissor.width = editor_viewport.view->viewport.width;
			editor_viewport.view->scissor.height = editor_viewport.view->viewport.height;

			if (auto* camera = editor_viewport.view->scene->GetComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity))
			{
				camera->SetAspectRatio(static_cast<float>(editor_viewport.view->viewport.width) / static_cast<float>(editor_viewport.view->viewport.height));
				if (editor_viewport.debug_settings.show_ddgi_overlay)
				{
					DrawDDGIDebugOverlay(
						*camera,
						editor_viewport.renderer_debug_state,
						viewport_pos,
						viewport_size,
						editor_viewport.debug_settings.show_ddgi_volume,
						editor_viewport.debug_settings.show_ddgi_probes,
						editor_viewport.debug_settings.show_ddgi_text,
						editor_viewport.debug_settings.ddgi_max_probe_draw_count);
				}
			}

			if (renderer && editor_viewport.debug_settings.show_renderer_stats)
			{
				const rendering::RendererDebugState& stats = editor_viewport.renderer_debug_state;
				ImGui::SetNextWindowPos(
					ImVec2(viewport_pos.x + 8.0f, viewport_pos.y + viewport_size.y - 8.0f),
					ImGuiCond_Always, ImVec2(0.0f, 1.0f));
				ImGui::SetNextWindowBgAlpha(0.5f);
				constexpr ImGuiWindowFlags overlay_flags =
					ImGuiWindowFlags_NoDecoration |
					ImGuiWindowFlags_NoDocking |
					ImGuiWindowFlags_AlwaysAutoResize |
					ImGuiWindowFlags_NoSavedSettings |
					ImGuiWindowFlags_NoFocusOnAppearing |
					ImGuiWindowFlags_NoNav |
					ImGuiWindowFlags_NoMove;
				if (ImGui::Begin("##renderer_stats", nullptr, overlay_flags))
				{
					ImGui::Text("Draw Calls : %u", stats.draw_call_count);
					ImGui::Text("Renderables: %u / %u (visible / total)", stats.visible_renderable_count, stats.total_renderable_count);
				}
				ImGui::End();
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		DrawProjectSettingsWindow(&show_project_settings_window);

		//ImGui::Begin("Scene Tree");
		//ImGui::Text("...");
		//ImGui::End();

		if (ImGui::Begin(editor_text::entity_list_window))
		{
			static int selected_index = -1;
			static ecs::Entity pending_delete_entity = INVALID_ENTITY;
			static ImVec2 delete_entity_popup_pos = {};
			ecs::Entity delete_entity = INVALID_ENTITY;
			bool open_delete_entity_confirm = false;

			Size running_import_count = 0;
			for (const std::shared_ptr<EditorAssetImporter::ImportTask>& task : asset_importer.tasks)
			{
				if (task && !task->finished.load())
				{
					++running_import_count;
				}
			}
			if (running_import_count > 0)
			{
				const int dot_count = static_cast<int>(ImGui::GetTime() * 3.0) % 4;
				std::string import_status = running_import_count == 1 ? editor_text::importing_asset : editor_text::importing_assets + std::to_string(running_import_count) + ")";
				import_status.append(dot_count, '.');
				ImGui::TextDisabled("%s", import_status.c_str());
				ImGui::Separator();
			}

			if (ImGui::Button("+"))
			{
				ecs::Entity entity = editor_viewport.view->scene->CreateEntity();
				auto transform = editor_viewport.view->scene->AddComponent<TransformComponent>(entity);
				auto name = editor_viewport.view->scene->AddComponent<NameComponent>(entity);
				UpdateEntityList();
				editor_viewport.picked_entity = entity;

				for (int i = 0; i < static_cast<int>(sorted_entities.size()); ++i)
				{
					if (sorted_entities[i] == entity)
					{
						selected_index = i;
						break;
					}
				}
			}

			if (ImGui::BeginChild("EntityListRegion", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar))
			{
				ImGuiListClipper clipper;
				clipper.Begin((int)sorted_entities.size());

				while (clipper.Step())
				{
					for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i)
					{
						const auto& id = sorted_entities[i];
						ImGui::PushID(static_cast<int>(id));

						std::string label = editor_text::entity_label + std::to_string(id);
						auto name = editor_viewport.view->scene->GetComponent<ecs::NameComponent>(id);
						if (name != nullptr)
						{
							label += " (" + name->value + ")";
						}

						const bool is_selected = (editor_viewport.picked_entity == id);
						if (ImGui::Selectable(label.c_str(), is_selected))
						{
							selected_index = i;
							editor_viewport.picked_entity = id;
						}

						if (ImGui::BeginPopupContextItem("EntityContextMenu"))
						{
							selected_index = i;
							editor_viewport.picked_entity = id;

							if (ImGui::MenuItem(editor_text::delete_entity, nullptr, false, id != editor_viewport.view->camera_entity))
							{
								pending_delete_entity = id;
								delete_entity_popup_pos = ImGui::GetMousePos();
								open_delete_entity_confirm = true;
							}

							ImGui::EndPopup();
						}

						if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
							selected_index = i;
						}

						ImGui::PopID();
					}
				}
			}

			ImGui::EndChild();

			if (open_delete_entity_confirm)
			{
				ImGui::OpenPopup(editor_text::delete_entity_confirm_popup);
			}

			ImGui::SetNextWindowPos(delete_entity_popup_pos, ImGuiCond_Appearing);
			if (ImGui::BeginPopup(editor_text::delete_entity_confirm_popup, ImGuiWindowFlags_AlwaysAutoResize))
			{
				String entity_label = editor_text::entity_label + std::to_string(pending_delete_entity);
				if (ecs::NameComponent* name = editor_viewport.view->scene->GetComponent<ecs::NameComponent>(pending_delete_entity))
				{
					entity_label += " (" + name->value + ")";
				}

				ImGui::Text(editor_text::delete_confirm_format, entity_label.c_str());
				ImGui::TextDisabled(editor_text::delete_entity_children_warning);

				const bool can_delete_entity = pending_delete_entity != INVALID_ENTITY && pending_delete_entity != editor_viewport.view->camera_entity;
				if (!can_delete_entity)
				{
					ImGui::BeginDisabled();
				}
				if (ImGui::Button(editor_text::delete_button))
				{
					delete_entity = pending_delete_entity;
					pending_delete_entity = INVALID_ENTITY;
					ImGui::CloseCurrentPopup();
				}
				if (!can_delete_entity)
				{
					ImGui::EndDisabled();
				}
				ImGui::SameLine();
				if (ImGui::Button(editor_text::cancel))
				{
					pending_delete_entity = INVALID_ENTITY;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (delete_entity != INVALID_ENTITY)
			{
				if (editor_viewport.picked_entity == delete_entity)
				{
					editor_viewport.picked_entity = INVALID_ENTITY;
				}
				selected_index = -1;

				eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, delete_entity](const won::function::Value&) {
					if (delete_entity == editor_viewport.view->camera_entity)
					{
						return;
					}

					Vector<ecs::Entity> entities_to_delete;
					entities_to_delete.push_back(delete_entity);

					auto hierarchy_array = editor_viewport.view->scene->GetComponentArray<ecs::HierarchyComponent>();
					if (hierarchy_array)
					{
						for (Size delete_index = 0; delete_index < entities_to_delete.size(); ++delete_index)
						{
							const ecs::Entity parent = entities_to_delete[delete_index];
							for (Size hierarchy_index = 0; hierarchy_index < hierarchy_array->data.size(); ++hierarchy_index)
							{
								if (hierarchy_array->data[hierarchy_index].parent_id != parent)
								{
									continue;
								}

								const ecs::Entity child = hierarchy_array->index_to_entity[hierarchy_index];
								if (std::find(entities_to_delete.begin(), entities_to_delete.end(), child) == entities_to_delete.end())
								{
									entities_to_delete.push_back(child);
								}
							}
						}
					}

					EditorViewport::DeferredResRemoval deferred_res_removal = {};
					deferred_res_removal.frames_left = 8;
					for (ecs::Entity entity : entities_to_delete)
					{
						ecs::GeometryComponent* geometry = editor_viewport.view->scene->GetComponent<ecs::GeometryComponent>(entity);
						if (geometry && geometry->mesh)
						{
							deferred_res_removal.meshes.push_back(geometry->mesh);
							if (geometry->mesh->render_data.buffer)
							{
								deferred_res_removal.resources.push_back(geometry->mesh->render_data.buffer);
							}
							if (geometry->mesh->gpu_bvh.node_buffer)
							{
								deferred_res_removal.resources.push_back(geometry->mesh->gpu_bvh.node_buffer);
							}
							if (geometry->mesh->gpu_bvh.primitive_buffer)
							{
								deferred_res_removal.resources.push_back(geometry->mesh->gpu_bvh.primitive_buffer);
							}
						}

						ecs::MaterialComponent* material = editor_viewport.view->scene->GetComponent<ecs::MaterialComponent>(entity);
						if (material && material->material)
						{
							for (resource::MaterialSlot& material_slot : material->material->slots)
							{
								for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
								{
									if (material_slot.textures[texture_slot].texture)
									{
										deferred_res_removal.resources.push_back(material_slot.textures[texture_slot].texture);
									}
								}
							}
						}
					}

					if (!deferred_res_removal.meshes.empty() || !deferred_res_removal.resources.empty())
					{
						editor_viewport.deferred_res_removals.push_back(std::move(deferred_res_removal));
					}

					editor_viewport.view->scene->DestroyEntity(delete_entity);
					const Vector<ecs::Entity>& entities = editor_viewport.view->scene->GetEntities();
					if (std::find(entities.begin(), entities.end(), editor_viewport.picked_entity) == entities.end())
					{
						editor_viewport.picked_entity = INVALID_ENTITY;
					}
					UpdateEntityList();
				});
			}
		}
		ImGui::End();

		if (ImGui::Begin(editor_text::inspector_window))
		{
			if (editor_viewport.picked_entity != INVALID_ENTITY)
			{
				// TODO: use reflection system
				NameComponent* name_comp = editor_viewport.view->scene->GetComponent<NameComponent>(editor_viewport.picked_entity);
				if (name_comp)
				{
					ImGui::PushID("NameComponent");
					ImGui::Text(editor_text::name_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::name_component);

					if (!remove_component)
					{
						char name_buf[256];
						std::snprintf(name_buf, sizeof(name_buf), "%s", name_comp->value.c_str());

						if (ImGui::InputText(editor_text::value, name_buf, sizeof(name_buf)))
						{
							name_comp->value = name_buf;
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<NameComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				TransformComponent* transform_comp = editor_viewport.view->scene->GetComponent<TransformComponent>(editor_viewport.picked_entity);
				if (transform_comp)
				{
					ImGui::PushID("TransformComponent");
					ImGui::Text(editor_text::transform_component);
					const bool can_remove_transform = editor_viewport.picked_entity != editor_viewport.view->camera_entity;
					bool remove_component = DrawComponentRemoveButton(editor_text::transform_component, can_remove_transform);

					if (!remove_component)
					{
						float position[3] = { transform_comp->position.x, transform_comp->position.y, transform_comp->position.z };
						if (ImGui::InputFloat3(editor_text::position, position))
						{
							transform_comp->position = { position[0], position[1], position[2] };
							transform_comp->SetDirty();
						}

						float3 rotation_xyz = QuaternionToEulerXYZDegrees(transform_comp->rotation);
						float rotation[3] = { rotation_xyz.x, rotation_xyz.y, rotation_xyz.z };
						if (ImGui::InputFloat3(editor_text::rotation_xyz, rotation))
						{
							transform_comp->rotation = EulerXYZDegreesToQuaternion({ rotation[0], rotation[1], rotation[2] });
							transform_comp->SetDirty();
						}

						float scale[3] = { transform_comp->scale.x, transform_comp->scale.y, transform_comp->scale.z };
						if (ImGui::InputFloat3(editor_text::scale, scale))
						{
							transform_comp->scale = { scale[0], scale[1], scale[2] };
							transform_comp->SetDirty();
						}
					}
					else if (can_remove_transform)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<TransformComponent>(entity);
							editor_viewport.view->scene->SetBVHDirty();
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				HierarchyComponent* hierarchy_comp = editor_viewport.view->scene->GetComponent<HierarchyComponent>(editor_viewport.picked_entity);
				if (hierarchy_comp)
				{
					ImGui::PushID("HierarchyComponent");
					ImGui::Text(editor_text::hierarchy_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::hierarchy_component);

					if (!remove_component)
					{
						uint64 parent_id = hierarchy_comp->parent_id;
						if (ImGui::InputScalar(editor_text::parent, ImGuiDataType_U64, &parent_id))
						{
							hierarchy_comp->parent_id = parent_id;
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<HierarchyComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				LightComponent* light_comp = editor_viewport.view->scene->GetComponent<LightComponent>(editor_viewport.picked_entity);
				if (light_comp)
				{
					ImGui::PushID("LightComponent");
					ImGui::Text(editor_text::light_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::light_component);
					
					if (!remove_component)
					{
						// TODO: enum is hard coded
						int light_type = static_cast<int>(light_comp->type);
						const char* light_type_items[] = { editor_text::directional, editor_text::point, editor_text::spot };
						if (ImGui::Combo(editor_text::type, &light_type, light_type_items, IM_ARRAYSIZE(light_type_items)))
						{
							light_comp->type = static_cast<LightComponent::LightType>(light_type);
						}

						float color[3] = { light_comp->color.x, light_comp->color.y, light_comp->color.z };
						if (ImGui::InputFloat3(editor_text::color, color))
						{
							light_comp->color = { color[0], color[1], color[2] };
						}

						ImGui::DragFloat(editor_text::intensity, &light_comp->intensity, 1.0f, 0.0f, 100000.0f);
						ImGui::DragFloat(editor_text::range, &light_comp->range, 0.1f, 0.0f, 100000.0f);
						ImGui::DragFloat(editor_text::outer_cone, &light_comp->outer_cone_angle, 0.01f, 0.0f, math::PI);
						ImGui::DragFloat(editor_text::inner_cone, &light_comp->inner_cone_angle, 0.01f, 0.0f, math::PI);

						int shadow_map_resolution = static_cast<int>(light_comp->shadow_map_resolution);
						if (ImGui::InputInt(editor_text::shadow_resolution, &shadow_map_resolution))
						{
							light_comp->shadow_map_resolution = (std::max)(1, shadow_map_resolution);
						}

						int shadow_cascade_count = static_cast<int>(light_comp->shadow_cascade_count);
						if (ImGui::SliderInt(editor_text::cascade_count, &shadow_cascade_count, 1, SHADOW_CASCADE_COUNT_MAX))
						{
							light_comp->shadow_cascade_count = static_cast<uint32>(shadow_cascade_count);
						}

						ImGui::SliderFloat(editor_text::cascade_lambda, &light_comp->shadow_cascade_lambda, 0.0f, 1.0f);
						ImGui::SliderFloat(editor_text::cascade_blend, &light_comp->shadow_cascade_blend, 0.0f, 0.3f);

						bool is_active = light_comp->IsActive();
						if (ImGui::Checkbox(editor_text::active, &is_active))
						{
							light_comp->SetActive(is_active);
						}

						bool is_dynamic = light_comp->IsDynamic();
						if (ImGui::Checkbox(editor_text::dynamic, &is_dynamic))
						{
							light_comp->SetDynamic(is_dynamic);
						}

						bool is_cast_shadow = light_comp->IsCastShadow();
						if (ImGui::Checkbox(editor_text::cast_shadow, &is_cast_shadow))
						{
							light_comp->SetCastShadow(is_cast_shadow);
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<LightComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				CameraComponent* camera_comp = editor_viewport.view->scene->GetComponent<CameraComponent>(editor_viewport.picked_entity);
				if (camera_comp)
				{
					ImGui::PushID("CameraComponent");
					ImGui::Text(editor_text::camera_component);
					const bool can_remove_camera = editor_viewport.picked_entity != editor_viewport.view->camera_entity;
					bool remove_component = DrawComponentRemoveButton(editor_text::camera_component, can_remove_camera);

					if (!remove_component)
					{
						bool is_ortho = camera_comp->IsOrtho();
						if (ImGui::Checkbox(editor_text::orthographic, &is_ortho))
						{
							camera_comp->SetOrtho(is_ortho);
						}

						float near_plane = camera_comp->near_plane;
						float far_plane = camera_comp->far_plane;
						bool near_far_changed = false;
						near_far_changed |= ImGui::DragFloat(editor_text::near_plane, &near_plane, 0.01f, 0.001f, 100000.0f);
						near_far_changed |= ImGui::DragFloat(editor_text::far_plane, &far_plane, 1.0f, 0.01f, 100000.0f);
						if (near_far_changed)
						{
							near_plane = (std::max)(0.001f, near_plane);
							far_plane = (std::max)(near_plane + 0.001f, far_plane);
							camera_comp->SetNearFar(near_plane, far_plane);
						}

						if (!camera_comp->IsOrtho())
						{
							float fov_y = camera_comp->fov_y;
							if (ImGui::DragFloat(editor_text::fov_y, &fov_y, 0.01f, 0.01f, math::PI - 0.01f))
							{
								camera_comp->SetFOV_Y(fov_y);
							}
						}
						else
						{
							float ortho_vertical_size = camera_comp->ortho_vertical_size;
							if (ImGui::DragFloat(editor_text::ortho_size, &ortho_vertical_size, 0.1f, 0.001f, 100000.0f))
							{
								camera_comp->SetOrthoVerticalSize(ortho_vertical_size);
							}
						}

						ImGui::Text(editor_text::aspect_ratio_format, camera_comp->aspect_ratio);
						ImGui::DragFloat(editor_text::aperture, &camera_comp->aperture, 0.01f, 0.0f, 128.0f);
						ImGui::DragFloat(editor_text::shutter_speed, &camera_comp->shutter_speed, 0.001f, 0.0001f, 100.0f);
						ImGui::DragFloat(editor_text::sensitivity, &camera_comp->sensitivity, 1.0f, 1.0f, 102400.0f);
					}
					else if (can_remove_camera)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							if (entity != editor_viewport.view->camera_entity)
							{
								editor_viewport.view->scene->RemoveComponent<CameraComponent>(entity);
							}
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				EnvironmentComponent* environment_comp = editor_viewport.view->scene->GetComponent<EnvironmentComponent>(editor_viewport.picked_entity);
				if (environment_comp)
				{
					ImGui::PushID("EnvironmentComponent");
					ImGui::Text(editor_text::environment_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::environment_component);

					if (!remove_component)
					{
						bool is_active = environment_comp->IsActive();
						if (ImGui::Checkbox(editor_text::active, &is_active))
						{
							environment_comp->SetActive(is_active);
						}

						int sky_type = static_cast<int>(environment_comp->sky_type);
						const char* sky_type_items[] = { editor_text::none, editor_text::procedural };
						if (ImGui::Combo(editor_text::sky_type, &sky_type, sky_type_items, IM_ARRAYSIZE(sky_type_items)))
						{
							environment_comp->sky_type = static_cast<EnvironmentComponent::SkyType>(sky_type);
						}

						float sun_direction[3] = { environment_comp->sun_direction.x, environment_comp->sun_direction.y, environment_comp->sun_direction.z };
						if (ImGui::DragFloat3(editor_text::sun_direction, sun_direction, 0.01f, -1.0f, 1.0f))
						{
							float3 direction = { sun_direction[0], sun_direction[1], sun_direction[2] };
							const float direction_length_sq = math::LengthSquared(direction);
							if (direction_length_sq > 0.0f)
							{
								const float inv_direction_length = 1.0f / std::sqrt(direction_length_sq);
								environment_comp->sun_direction =
								{
									direction.x * inv_direction_length,
									direction.y * inv_direction_length,
									direction.z * inv_direction_length,
								};
							}
						}

						float sun_color[3] = { environment_comp->sun_color.x, environment_comp->sun_color.y, environment_comp->sun_color.z };
						if (ImGui::InputFloat3(editor_text::sun_color, sun_color))
						{
							environment_comp->sun_color = { sun_color[0], sun_color[1], sun_color[2] };
						}

						ImGui::DragFloat(editor_text::sun_intensity, &environment_comp->sun_intensity, 0.1f, 0.0f, 100000.0f);
						ImGui::DragFloat(editor_text::sun_angular_radius, &environment_comp->sun_angular_radius, 0.0001f, 0.0f, 1.0f);
						ImGui::DragFloat(editor_text::sun_glow_intensity, &environment_comp->sun_glow_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat(editor_text::sun_glow_falloff, &environment_comp->sun_glow_falloff, 0.1f, 0.0f, 1000.0f);

						float sky_horizon_color[3] = { environment_comp->sky_horizon_color.x, environment_comp->sky_horizon_color.y, environment_comp->sky_horizon_color.z };
						if (ImGui::InputFloat3(editor_text::sky_horizon_color, sky_horizon_color))
						{
							environment_comp->sky_horizon_color = { sky_horizon_color[0], sky_horizon_color[1], sky_horizon_color[2] };
						}

						float sky_zenith_color[3] = { environment_comp->sky_zenith_color.x, environment_comp->sky_zenith_color.y, environment_comp->sky_zenith_color.z };
						if (ImGui::InputFloat3(editor_text::sky_zenith_color, sky_zenith_color))
						{
							environment_comp->sky_zenith_color = { sky_zenith_color[0], sky_zenith_color[1], sky_zenith_color[2] };
						}

						ImGui::DragFloat(editor_text::sky_intensity, &environment_comp->sky_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat(editor_text::sky_horizon_falloff, &environment_comp->sky_horizon_falloff, 0.01f, 0.0f, 1000.0f);

						float ground_horizon_color[3] = { environment_comp->ground_horizon_color.x, environment_comp->ground_horizon_color.y, environment_comp->ground_horizon_color.z };
						if (ImGui::InputFloat3(editor_text::ground_horizon_color, ground_horizon_color))
						{
							environment_comp->ground_horizon_color = { ground_horizon_color[0], ground_horizon_color[1], ground_horizon_color[2] };
						}

						float ground_color[3] = { environment_comp->ground_color.x, environment_comp->ground_color.y, environment_comp->ground_color.z };
						if (ImGui::InputFloat3(editor_text::ground_color, ground_color))
						{
							environment_comp->ground_color = { ground_color[0], ground_color[1], ground_color[2] };
						}

						ImGui::DragFloat(editor_text::ground_intensity, &environment_comp->ground_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat(editor_text::ground_falloff, &environment_comp->ground_falloff, 0.01f, 0.0f, 1000.0f);

						int gi_mode = static_cast<int>(environment_comp->gi_mode);
						const char* gi_mode_items[] = { editor_text::none, editor_text::ambient, editor_text::ddgi };
						if (ImGui::Combo(editor_text::gi_mode, &gi_mode, gi_mode_items, IM_ARRAYSIZE(gi_mode_items)))
						{
							environment_comp->gi_mode = static_cast<EnvironmentComponent::GIMode>(gi_mode);
						}

						float ambient_color[3] = { environment_comp->ambient_color.x, environment_comp->ambient_color.y, environment_comp->ambient_color.z };
						if (ImGui::InputFloat3(editor_text::ambient_color, ambient_color))
						{
							environment_comp->ambient_color = { ambient_color[0], ambient_color[1], ambient_color[2] };
						}

						ImGui::DragFloat(editor_text::ambient_intensity, &environment_comp->ambient_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat(editor_text::indirect_diffuse_scale, &environment_comp->indirect_diffuse_scale, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat(editor_text::indirect_specular_scale, &environment_comp->indirect_specular_scale, 0.01f, 0.0f, 1000.0f);
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<EnvironmentComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				FogVolumeComponent* fog_volume_comp = editor_viewport.view->scene->GetComponent<FogVolumeComponent>(editor_viewport.picked_entity);
				if (fog_volume_comp)
				{
					ImGui::PushID("FogVolumeComponent");
					ImGui::Text(editor_text::fog_volume_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::fog_volume_component);

					if (!remove_component)
					{

					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<FogVolumeComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				DDGIVolumeComponent* ddgi_volume_comp = editor_viewport.view->scene->GetComponent<DDGIVolumeComponent>(editor_viewport.picked_entity);
				if (ddgi_volume_comp)
				{
					ImGui::PushID("DDGIVolumeComponent");
					ImGui::Text(editor_text::ddgi_volume_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::ddgi_volume_component);

					if (!remove_component)
					{
						bool is_active = ddgi_volume_comp->IsActive();
						if (ImGui::Checkbox(editor_text::active, &is_active))
						{
							ddgi_volume_comp->SetActive(is_active);
						}

						bool is_dynamic = ddgi_volume_comp->IsDynamic();
						if (ImGui::Checkbox(editor_text::dynamic, &is_dynamic))
						{
							ddgi_volume_comp->SetDynamic(is_dynamic);
						}

						int probe_counts[3] = {
							static_cast<int>(ddgi_volume_comp->probe_counts.x),
							static_cast<int>(ddgi_volume_comp->probe_counts.y),
							static_cast<int>(ddgi_volume_comp->probe_counts.z)
						};
						if (ImGui::InputInt3(editor_text::probe_counts, probe_counts))
						{
							ddgi_volume_comp->probe_counts = {
								static_cast<uint32>((std::max)(1, probe_counts[0])),
								static_cast<uint32>((std::max)(1, probe_counts[1])),
								static_cast<uint32>((std::max)(1, probe_counts[2]))
							};
						}

						float probe_spacing[3] = { ddgi_volume_comp->probe_spacing.x, ddgi_volume_comp->probe_spacing.y, ddgi_volume_comp->probe_spacing.z };
						if (ImGui::InputFloat3(editor_text::probe_spacing, probe_spacing))
						{
							ddgi_volume_comp->probe_spacing = { probe_spacing[0], probe_spacing[1], probe_spacing[2] };
						}

						float volume_offset[3] = { ddgi_volume_comp->volume_offset.x, ddgi_volume_comp->volume_offset.y, ddgi_volume_comp->volume_offset.z };
						if (ImGui::InputFloat3(editor_text::volume_offset, volume_offset))
						{
							ddgi_volume_comp->volume_offset = { volume_offset[0], volume_offset[1], volume_offset[2] };
						}

						int probes_per_frame = static_cast<int>(ddgi_volume_comp->probes_per_frame);
						if (ImGui::InputInt(editor_text::probes_per_frame, &probes_per_frame))
						{
							ddgi_volume_comp->probes_per_frame = static_cast<uint32>((std::max)(1, probes_per_frame));
						}

						int priority = static_cast<int>(ddgi_volume_comp->priority);
						if (ImGui::InputInt(editor_text::priority, &priority))
						{
							ddgi_volume_comp->priority = static_cast<uint32>((std::max)(0, priority));
						}

						ImGui::SliderFloat(editor_text::hysteresis, &ddgi_volume_comp->hysteresis, 0.0f, 1.0f);
						ImGui::DragFloat(editor_text::normal_bias, &ddgi_volume_comp->normal_bias, 0.001f, 0.0f, 100.0f);
						ImGui::DragFloat(editor_text::view_bias, &ddgi_volume_comp->view_bias, 0.001f, 0.0f, 100.0f);
						ImGui::DragFloat(editor_text::max_distance, &ddgi_volume_comp->max_distance, 0.01f, 0.0f, 100000.0f);
						if (ImGui::Button(editor_text::update_scene_gpubvh))
						{
							editor_viewport.view->scene->BuildGPUBVH();
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<DDGIVolumeComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				GeometryComponent* geometry_comp = editor_viewport.view->scene->GetComponent<GeometryComponent>(editor_viewport.picked_entity);
				if (geometry_comp)
				{
					ImGui::PushID("GeometryComponent");
					ImGui::Text(editor_text::geometry_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::geometry_component);

					if (!remove_component)
					{
						ImGui::Text(editor_text::mesh_format, geometry_comp->mesh ? editor_text::assigned : editor_text::none);

						bool cast_shadow = geometry_comp->IsCastShadow();
						if (ImGui::Checkbox(editor_text::cast_shadow, &cast_shadow))
						{
							geometry_comp->SetCastShadow(cast_shadow);
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							EditorViewport::DeferredResRemoval deferred_res_removal = {};
							deferred_res_removal.frames_left = 8;

							ecs::GeometryComponent* geometry = editor_viewport.view->scene->GetComponent<ecs::GeometryComponent>(entity);
							if (geometry && geometry->mesh)
							{
								deferred_res_removal.meshes.push_back(geometry->mesh);
								if (geometry->mesh->render_data.buffer)
								{
									deferred_res_removal.resources.push_back(geometry->mesh->render_data.buffer);
								}
								if (geometry->mesh->gpu_bvh.node_buffer)
								{
									deferred_res_removal.resources.push_back(geometry->mesh->gpu_bvh.node_buffer);
								}
								if (geometry->mesh->gpu_bvh.primitive_buffer)
								{
									deferred_res_removal.resources.push_back(geometry->mesh->gpu_bvh.primitive_buffer);
								}
							}

							if (!deferred_res_removal.meshes.empty() || !deferred_res_removal.resources.empty())
							{
								editor_viewport.deferred_res_removals.push_back(std::move(deferred_res_removal));
							}

							editor_viewport.view->scene->RemoveComponent<GeometryComponent>(entity);
							editor_viewport.view->scene->SetBVHDirty();
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Collider3DComponent* collider_3d_comp = editor_viewport.view->scene->GetComponent<Collider3DComponent>(editor_viewport.picked_entity);
				if (collider_3d_comp)
				{
					ImGui::PushID("Collider3DComponent");
					ImGui::Text(editor_text::collider_3d_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::collider_3d_component);

					if (!remove_component)
					{
						int shape_type = static_cast<int>(collider_3d_comp->shape_type);
						const char* shape_type_items[] = { editor_text::box, editor_text::sphere };
						if (ImGui::Combo(editor_text::type, &shape_type, shape_type_items, arraysize(shape_type_items)))
						{
							collider_3d_comp->shape_type = static_cast<Collider3DComponent::ShapeType>(shape_type);
							collider_3d_comp->SetDirty();
						}

						bool enabled = collider_3d_comp->IsEnabled();
						if (ImGui::Checkbox(editor_text::enabled, &enabled))
						{
							collider_3d_comp->SetEnabled(enabled);
						}

						bool trigger = collider_3d_comp->IsTrigger();
						if (ImGui::Checkbox(editor_text::trigger, &trigger))
						{
							collider_3d_comp->SetTrigger(trigger);
						}

						float offset[3] = { collider_3d_comp->offset.x, collider_3d_comp->offset.y, collider_3d_comp->offset.z };
						if (ImGui::InputFloat3(editor_text::offset, offset))
						{
							collider_3d_comp->offset = { offset[0], offset[1], offset[2] };
							collider_3d_comp->SetDirty();
						}

						float half_extent[3] = { collider_3d_comp->half_extent.x, collider_3d_comp->half_extent.y, collider_3d_comp->half_extent.z };
						if (ImGui::InputFloat3(editor_text::half_extent, half_extent))
						{
							collider_3d_comp->half_extent = {
								(std::max)(0.0f, half_extent[0]),
								(std::max)(0.0f, half_extent[1]),
								(std::max)(0.0f, half_extent[2])
							};
							collider_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(editor_text::radius, &collider_3d_comp->radius))
						{
							collider_3d_comp->radius = (std::max)(0.0f, collider_3d_comp->radius);
							collider_3d_comp->SetDirty();
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<Collider3DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Rigidbody3DComponent* rigidbody_3d_comp = editor_viewport.view->scene->GetComponent<Rigidbody3DComponent>(editor_viewport.picked_entity);
				if (rigidbody_3d_comp)
				{
					ImGui::PushID("Rigidbody3DComponent");
					ImGui::Text(editor_text::rigidbody_3d_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::rigidbody_3d_component);

					if (!remove_component)
					{
						int motion_type = static_cast<int>(rigidbody_3d_comp->motion_type);
						const char* motion_type_items[] = { editor_text::static_str, editor_text::kinematic, editor_text::dynamic };
						if (ImGui::Combo(editor_text::motion_type, &motion_type, motion_type_items, arraysize(motion_type_items)))
						{
							rigidbody_3d_comp->motion_type = static_cast<Rigidbody3DComponent::MotionType>(motion_type);
							rigidbody_3d_comp->SetDirty();
						}

						bool enabled = rigidbody_3d_comp->IsEnabled();
						if (ImGui::Checkbox(editor_text::enabled, &enabled))
						{
							rigidbody_3d_comp->SetEnabled(enabled);
						}

						if (ImGui::InputFloat(editor_text::mass, &rigidbody_3d_comp->mass))
						{
							rigidbody_3d_comp->mass = (std::max)(0.001f, rigidbody_3d_comp->mass);
							rigidbody_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(editor_text::friction, &rigidbody_3d_comp->friction))
						{
							rigidbody_3d_comp->friction = (std::max)(0.0f, rigidbody_3d_comp->friction);
							rigidbody_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(editor_text::restitution, &rigidbody_3d_comp->restitution))
						{
							rigidbody_3d_comp->restitution = (std::max)(0.0f, (std::min)(1.0f, rigidbody_3d_comp->restitution));
							rigidbody_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(editor_text::gravity_factor, &rigidbody_3d_comp->gravity_factor))
						{
							rigidbody_3d_comp->SetDirty();
						}

						float linear_vel[3] = { rigidbody_3d_comp->linear_velocity.x, rigidbody_3d_comp->linear_velocity.y, rigidbody_3d_comp->linear_velocity.z };
						if (ImGui::InputFloat3(editor_text::linear_velocity, linear_vel))
						{
							rigidbody_3d_comp->linear_velocity = { linear_vel[0], linear_vel[1], linear_vel[2] };
							rigidbody_3d_comp->SetDirty();
						}

						float angular_vel[3] = { rigidbody_3d_comp->angular_velocity.x, rigidbody_3d_comp->angular_velocity.y, rigidbody_3d_comp->angular_velocity.z };
						if (ImGui::InputFloat3(editor_text::angular_velocity, angular_vel))
						{
							rigidbody_3d_comp->angular_velocity = { angular_vel[0], angular_vel[1], angular_vel[2] };
							rigidbody_3d_comp->SetDirty();
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<Rigidbody3DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				AudioSourceComponent* audio_source_comp = editor_viewport.view->scene->GetComponent<AudioSourceComponent>(editor_viewport.picked_entity);
				if (audio_source_comp)
				{
					ImGui::PushID("AudioSourceComponent");
					ImGui::Text(editor_text::audio_source_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::audio_source_component);

					if (!remove_component)
					{
						bool enabled = audio_source_comp->IsEnabled();
						if (ImGui::Checkbox(editor_text::enabled, &enabled))
						{
							audio_source_comp->SetEnabled(enabled);
						}

						char sound_path[1024];
						strncpy(sound_path, audio_source_comp->sound_asset_path.c_str(), sizeof(sound_path) - 1);
						sound_path[sizeof(sound_path) - 1] = '\0';
						if (ImGui::InputText(editor_text::sound_asset_path, sound_path, sizeof(sound_path)))
						{
							audio_source_comp->sound_asset_path = sound_path;
							audio_source_comp->SetDirty();
						}

						if (ImGui::SliderFloat(editor_text::volume, &audio_source_comp->volume, 0.0f, 2.0f))
						{
							audio_source_comp->volume = (std::max)(0.0f, audio_source_comp->volume);
						}

						if (ImGui::SliderFloat(editor_text::pitch, &audio_source_comp->pitch, 0.01f, 4.0f))
						{
							audio_source_comp->pitch = (std::max)(0.001f, audio_source_comp->pitch);
						}

						bool is_3d = audio_source_comp->Is3D();
						if (ImGui::Checkbox(editor_text::is_3d, &is_3d))
						{
							audio_source_comp->Set3D(is_3d);
						}

						if (is_3d)
						{
							if (ImGui::InputFloat(editor_text::min_distance, &audio_source_comp->min_distance))
							{
								audio_source_comp->min_distance = (std::max)(0.001f, audio_source_comp->min_distance);
								audio_source_comp->SetDirty();
							}
							if (ImGui::InputFloat(editor_text::max_distance, &audio_source_comp->max_distance))
							{
								audio_source_comp->max_distance = (std::max)(audio_source_comp->min_distance, audio_source_comp->max_distance);
								audio_source_comp->SetDirty();
							}
						}

						bool loop = audio_source_comp->IsLoop();
						if (ImGui::Checkbox(editor_text::loop, &loop))
						{
							audio_source_comp->SetLoop(loop);
						}

						bool play_on_start = audio_source_comp->IsPlayOnStart();
						if (ImGui::Checkbox(editor_text::play_on_start, &play_on_start))
						{
							audio_source_comp->SetPlayOnStart(play_on_start);
						}

						bool is_playing = audio_source_comp->IsPlaying();
						if (ImGui::Checkbox(editor_text::is_playing, &is_playing))
						{
							audio_source_comp->SetPlaying(is_playing);
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<AudioSourceComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				AudioListenerComponent* audio_listener_comp = editor_viewport.view->scene->GetComponent<AudioListenerComponent>(editor_viewport.picked_entity);
				if (audio_listener_comp)
				{
					ImGui::PushID("AudioListenerComponent");
					ImGui::Text(editor_text::audio_listener_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::audio_listener_component);

					if (!remove_component)
					{
						ImGui::Checkbox(editor_text::enabled, &audio_listener_comp->enabled);
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<AudioListenerComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Sprite2DComponent* sprite_2d_comp = editor_viewport.view->scene->GetComponent<Sprite2DComponent>(editor_viewport.picked_entity);
				if (sprite_2d_comp)
				{
					ImGui::PushID("Sprite2DComponent");
					ImGui::Text(editor_text::sprite_2d_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::sprite_2d_component);

					if (!remove_component)
					{
						float anchor[2] = { sprite_2d_comp->anchor.x, sprite_2d_comp->anchor.y };
						if (ImGui::InputFloat2(editor_text::anchor, anchor))
						{
							sprite_2d_comp->anchor = { anchor[0], anchor[1] };
							sprite_2d_comp->SetDirty();
						}

						float position[2] = { sprite_2d_comp->position.x, sprite_2d_comp->position.y };
						if (ImGui::InputFloat2(editor_text::position, position))
						{
							sprite_2d_comp->position = { position[0], position[1] };
							sprite_2d_comp->SetDirty();
						}

						float size[2] = { sprite_2d_comp->size.x, sprite_2d_comp->size.y };
						if (ImGui::InputFloat2(editor_text::size, size))
						{
							sprite_2d_comp->size = { size[0], size[1] };
							sprite_2d_comp->SetDirty();
						}

						float pivot[2] = { sprite_2d_comp->pivot.x, sprite_2d_comp->pivot.y };
						if (ImGui::InputFloat2(editor_text::pivot, pivot))
						{
							sprite_2d_comp->pivot = { pivot[0], pivot[1] };
							sprite_2d_comp->SetDirty();
						}

						float uv_rect[4] = { sprite_2d_comp->uv_rect.x, sprite_2d_comp->uv_rect.y, sprite_2d_comp->uv_rect.z, sprite_2d_comp->uv_rect.w };
						if (ImGui::InputFloat4(editor_text::uv_rect, uv_rect))
						{
							sprite_2d_comp->uv_rect = { uv_rect[0], uv_rect[1], uv_rect[2], uv_rect[3] };
							sprite_2d_comp->SetDirty();
						}

						if (ImGui::InputInt(editor_text::layer, &sprite_2d_comp->layer))
						{
							sprite_2d_comp->SetDirty();
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<Sprite2DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Text2DComponent* text_2d_comp = editor_viewport.view->scene->GetComponent<Text2DComponent>(editor_viewport.picked_entity);
				if (text_2d_comp)
				{
					ImGui::PushID("Text2DComponent");
					ImGui::Text(editor_text::text_2d_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::text_2d_component);

					if (!remove_component)
					{
						ImGui::Text(editor_text::font_format, text_2d_comp->font && text_2d_comp->font->IsValid() ? editor_text::assigned : editor_text::none);

						char text_buf[4096] = {};
						strncpy_s(text_buf, text_2d_comp->text.c_str(), sizeof(text_buf) - 1);
						if (ImGui::InputTextMultiline(editor_text::text, text_buf, sizeof(text_buf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
						{
							text_2d_comp->text = text_buf;
							text_2d_comp->SetDirty();
						}

						float anchor[2] = { text_2d_comp->anchor.x, text_2d_comp->anchor.y };
						if (ImGui::InputFloat2(editor_text::anchor, anchor))
						{
							text_2d_comp->anchor = { anchor[0], anchor[1] };
							text_2d_comp->SetDirty();
						}

						float position[2] = { text_2d_comp->position.x, text_2d_comp->position.y };
						if (ImGui::InputFloat2(editor_text::position, position))
						{
							text_2d_comp->position = { position[0], position[1] };
							text_2d_comp->SetDirty();
						}

						int pixel_height = static_cast<int>(text_2d_comp->pixel_height);
						if (ImGui::InputInt(editor_text::pixel_height, &pixel_height))
						{
							text_2d_comp->pixel_height = static_cast<uint32>((std::max)(1, pixel_height));
							text_2d_comp->SetDirty();
						}

						float pivot[2] = { text_2d_comp->pivot.x, text_2d_comp->pivot.y };
						if (ImGui::InputFloat2(editor_text::pivot, pivot))
						{
							text_2d_comp->pivot = { pivot[0], pivot[1] };
							text_2d_comp->SetDirty();
						}

						if (ImGui::InputInt(editor_text::layer, &text_2d_comp->layer))
						{
							text_2d_comp->SetDirty();
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<Text2DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Sprite3DComponent* sprite_3d_comp = editor_viewport.view->scene->GetComponent<Sprite3DComponent>(editor_viewport.picked_entity);
				if (sprite_3d_comp)
				{
					ImGui::PushID("Sprite3DComponent");
					ImGui::Text(editor_text::sprite_3d_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::sprite_3d_component);

					if (!remove_component)
					{
						float size[2] = { sprite_3d_comp->size.x, sprite_3d_comp->size.y };
						if (ImGui::InputFloat2(editor_text::size, size))
						{
							sprite_3d_comp->size = { size[0], size[1] };
							sprite_3d_comp->SetDirty();
						}

						float pivot[2] = { sprite_3d_comp->pivot.x, sprite_3d_comp->pivot.y };
						if (ImGui::InputFloat2(editor_text::pivot, pivot))
						{
							sprite_3d_comp->pivot = { pivot[0], pivot[1] };
							sprite_3d_comp->SetDirty();
						}

						float uv_rect[4] = { sprite_3d_comp->uv_rect.x, sprite_3d_comp->uv_rect.y, sprite_3d_comp->uv_rect.z, sprite_3d_comp->uv_rect.w };
						if (ImGui::InputFloat4(editor_text::uv_rect, uv_rect))
						{
							sprite_3d_comp->uv_rect = { uv_rect[0], uv_rect[1], uv_rect[2], uv_rect[3] };
							sprite_3d_comp->SetDirty();
						}

						bool billboard = sprite_3d_comp->IsBillboard();
						if (ImGui::Checkbox(editor_text::billboard, &billboard))
						{
							sprite_3d_comp->SetBillboard(billboard);
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<Sprite3DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Text3DComponent* text_3d_comp = editor_viewport.view->scene->GetComponent<Text3DComponent>(editor_viewport.picked_entity);
				if (text_3d_comp)
				{
					ImGui::PushID("Text3DComponent");
					ImGui::Text(editor_text::text_3d_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::text_3d_component);

					if (!remove_component)
					{
						ImGui::Text(editor_text::font_format, text_3d_comp->font && text_3d_comp->font->IsValid() ? editor_text::assigned : editor_text::none);

						char text_buf[4096] = {};
						strncpy_s(text_buf, text_3d_comp->text.c_str(), sizeof(text_buf) - 1);
						if (ImGui::InputTextMultiline(editor_text::text, text_buf, sizeof(text_buf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
						{
							text_3d_comp->text = text_buf;
							text_3d_comp->SetDirty();
						}

						int pixel_height = static_cast<int>(text_3d_comp->pixel_height);
						if (ImGui::InputInt(editor_text::pixel_height, &pixel_height))
						{
							text_3d_comp->pixel_height = static_cast<uint32>((std::max)(1, pixel_height));
							text_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(editor_text::height, &text_3d_comp->height))
						{
							text_3d_comp->height = (std::max)(0.0f, text_3d_comp->height);
							text_3d_comp->SetDirty();
						}

						float pivot[2] = { text_3d_comp->pivot.x, text_3d_comp->pivot.y };
						if (ImGui::InputFloat2(editor_text::pivot, pivot))
						{
							text_3d_comp->pivot = { pivot[0], pivot[1] };
							text_3d_comp->SetDirty();
						}

						bool billboard = text_3d_comp->IsBillboard();
						if (ImGui::Checkbox(editor_text::billboard, &billboard))
						{
							text_3d_comp->SetBillboard(billboard);
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<Text3DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				AnimationComponent* animation_comp = editor_viewport.view->scene->GetComponent<AnimationComponent>(editor_viewport.picked_entity);
				if (animation_comp)
				{
					ImGui::PushID("AnimationComponent");
					ImGui::Text(editor_text::animation_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::animation_component);

					if (!remove_component)
					{
						const int clip_count = static_cast<int>(animation_comp->clips.size());
						ImGui::Text(editor_text::clips_format, clip_count);

						if (clip_count > 0)
						{
							if (animation_comp->current_clip_index >= animation_comp->clips.size())
							{
								animation_comp->current_clip_index = 0;
							}

							int current_clip_index = static_cast<int>(animation_comp->current_clip_index);
							std::shared_ptr<resource::AnimationClip> current_clip = animation_comp->clips[animation_comp->current_clip_index];
							String current_clip_name = current_clip && !current_clip->name.empty() ? current_clip->name : String(editor_text::clip) + " " + std::to_string(current_clip_index);
							if (ImGui::BeginCombo(editor_text::clip, current_clip_name.c_str()))
							{
								for (int clip_index = 0; clip_index < clip_count; ++clip_index)
								{
									const std::shared_ptr<resource::AnimationClip>& clip = animation_comp->clips[clip_index];
									String clip_name = clip && !clip->name.empty() ? clip->name : String(editor_text::clip) + " " + std::to_string(clip_index);
									const bool is_selected = current_clip_index == clip_index;
									if (ImGui::Selectable(clip_name.c_str(), is_selected))
									{
										animation_comp->current_clip_index = static_cast<uint32>(clip_index);
										animation_comp->time = 0.0f;
										animation_comp->bone_matrices_dirty = true;
									}
									if (is_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
								}
								ImGui::EndCombo();
							}

							current_clip = animation_comp->clips[animation_comp->current_clip_index];
							const float ticks_per_second = current_clip && current_clip->ticks_per_second > 0.0f ? current_clip->ticks_per_second : 1.0f;
							const float duration_seconds = current_clip ? current_clip->duration / ticks_per_second : 0.0f;

							if (ImGui::Button(animation_comp->playing ? editor_text::pause : editor_text::play))
							{
								animation_comp->playing = !animation_comp->playing;
							}
							ImGui::SameLine();
							ImGui::Checkbox(editor_text::loop, &animation_comp->loop);

							ImGui::DragFloat(editor_text::speed, &animation_comp->speed, 0.01f, -10.0f, 10.0f);

							if (duration_seconds > 0.0f)
							{
								float time = std::clamp(animation_comp->time, 0.0f, duration_seconds);
								if (ImGui::SliderFloat(editor_text::time, &time, 0.0f, duration_seconds))
								{
									animation_comp->time = time;
									animation_comp->bone_matrices_dirty = true;
								}
								ImGui::Text(editor_text::duration_format, duration_seconds);
							}
							else
							{
								ImGui::TextDisabled(editor_text::duration_zero);
							}
						}
						else
						{
							ImGui::TextDisabled(editor_text::no_animation_clips);
						}

						ImGui::Text(editor_text::bone_matrices_format, static_cast<int>(animation_comp->bone_matrices.size()));
						ImGui::Text(editor_text::bone_matrix_offset_format, animation_comp->bone_matrix_offset);
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<AnimationComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				MaterialComponent* material_comp = editor_viewport.view->scene->GetComponent<MaterialComponent>(editor_viewport.picked_entity);
				if (material_comp)
				{
					ImGui::PushID("MaterialComponent");
					ImGui::Text(editor_text::material_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::material_component);

					if (!remove_component)
					{
						static Entity selected_material_entity = INVALID_ENTITY;
						static int selected_material_slot = 0;
						if (selected_material_entity != editor_viewport.picked_entity)
						{
							selected_material_entity = editor_viewport.picked_entity;
							selected_material_slot = 0;
						}

						int material_slot_count = static_cast<int>(material_comp->GetMaterialSlotCount());
						ImGui::Text(editor_text::material_slots_format, material_slot_count);

						// Fork is meaningful when the material is shared: cache-backed (has a path) or
						// referenced by more than this component. Mirrors ForkMaterial()'s guard.
						const bool fork_meaningful = material_comp->material
							&& (!material_comp->material_asset_path.empty() || material_comp->material.use_count() > 1);
						ImGui::BeginDisabled(!fork_meaningful);
						if (ImGui::Button(editor_text::fork_material))
						{
							material_comp->ForkMaterial();
						}
						ImGui::EndDisabled();

						// Promote an inline (forked) material to a shared .wonmat asset.
						ImGui::SameLine();
						ImGui::BeginDisabled(!material_comp->material || !material_comp->material_asset_path.empty());
						if (ImGui::Button(editor_text::save_material_asset))
						{
							open_save_material_asset = true;
						}
						ImGui::EndDisabled();

						if (ImGui::Button(editor_text::add_slot))
						{
							material_comp->AddMaterialSlot();
							selected_material_slot = material_slot_count;
							material_slot_count = static_cast<int>(material_comp->GetMaterialSlotCount());
						}
						ImGui::SameLine();
						if (material_slot_count == 0)
						{
							ImGui::BeginDisabled();
						}
						if (ImGui::Button(editor_text::remove_slot) && material_slot_count > 0)
						{
							material_comp->material->slots.erase(material_comp->material->slots.begin() + selected_material_slot);
							material_comp->SetDirty();
							material_slot_count = static_cast<int>(material_comp->GetMaterialSlotCount());
							selected_material_slot = (std::max)(0, material_slot_count - 1);
						}
						if (material_slot_count == 0)
						{
							ImGui::EndDisabled();
						}

						if (material_slot_count > 0)
						{
							selected_material_slot = (std::min)(selected_material_slot, material_slot_count - 1);

							String slot_label = String(editor_text::slot_prefix) + std::to_string(selected_material_slot);
							if (ImGui::BeginCombo(editor_text::selected_slot, slot_label.c_str()))
							{
								for (int slot_index = 0; slot_index < material_slot_count; ++slot_index)
								{
									String item_label = String(editor_text::slot_prefix) + std::to_string(slot_index);
									const bool is_selected = selected_material_slot == slot_index;
									if (ImGui::Selectable(item_label.c_str(), is_selected))
									{
										selected_material_slot = slot_index;
									}
									if (is_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
								}
								ImGui::EndCombo();
							}

							resource::MaterialSlot& material_slot = material_comp->GetMaterialSlot(static_cast<uint32>(selected_material_slot));
							bool material_changed = false;
							const char* material_type_items[] = { editor_text::unlit, editor_text::pbr };
							int material_type = static_cast<int>(material_slot.material_type);
							if (ImGui::Combo(editor_text::shader_type, &material_type, material_type_items, IM_ARRAYSIZE(material_type_items)))
							{
								material_slot.material_type = static_cast<resource::MaterialType>(material_type);
								material_changed = true;
							}

							const char* blend_mode_items[] = { editor_text::opaque, editor_text::alpha, editor_text::additive, editor_text::premultiplied };
							int blend_mode = static_cast<int>(material_slot.blend_mode);
							if (ImGui::Combo(editor_text::blend_mode, &blend_mode, blend_mode_items, IM_ARRAYSIZE(blend_mode_items)))
							{
								material_slot.blend_mode = static_cast<resource::MaterialBlendMode>(blend_mode);
								material_changed = true;
							}

							material_changed |= ImGui::Checkbox(editor_text::double_sided, &material_slot.double_sided);
							material_changed |= ImGui::Checkbox(editor_text::use_vertex_colors, &material_slot.use_vertex_colors);
							material_changed |= ImGui::Checkbox(editor_text::receive_shadow, &material_slot.receive_shadow);

							float base_color[4] = { material_slot.base_color.x, material_slot.base_color.y, material_slot.base_color.z, material_slot.base_color.w };
							if (ImGui::ColorEdit4(editor_text::base_color, base_color))
							{
								material_slot.base_color = { base_color[0], base_color[1], base_color[2], base_color[3] };
								material_changed = true;
							}

							material_changed |= ImGui::SliderFloat(editor_text::metallic, &material_slot.metallic, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(editor_text::roughness, &material_slot.roughness, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(editor_text::reflectance, &material_slot.reflectance, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(editor_text::anisotropy, &material_slot.anisotropy, -1.0f, 1.0f);

							float sheen_color[3] = { material_slot.sheen_color.x, material_slot.sheen_color.y, material_slot.sheen_color.z };
							if (ImGui::ColorEdit3(editor_text::sheen_color, sheen_color))
							{
								material_slot.sheen_color = { sheen_color[0], sheen_color[1], sheen_color[2] };
								material_changed = true;
							}

							material_changed |= ImGui::SliderFloat(editor_text::sheen_roughness, &material_slot.sheen_roughness, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(editor_text::clearcoat, &material_slot.clearcoat, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(editor_text::clearcoat_roughness, &material_slot.clearcoat_roughness, 0.0f, 1.0f);

							static const char* texture_slot_names[] = {
								editor_text::base_color_map,
								editor_text::normal_map,
								editor_text::emissive_map,
								editor_text::opacity_map,
								editor_text::displacement_map,
								editor_text::occlusion_map,
								editor_text::sheen_color_map,
								editor_text::sheen_roughness_map,
								editor_text::clearcoat_map,
								editor_text::clearcoat_roughness_map,
								editor_text::clearcoat_normal_map,
								editor_text::anisotropy_map,
								editor_text::roughness_map,
								editor_text::metallic_map,
							};
							static_assert(TEXTURESLOT_COUNT == IM_ARRAYSIZE(texture_slot_names), "Texture slot labels must match");

							ImGui::SeparatorText(editor_text::textures);
							for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
							{
								const resource::MaterialSlot::TextureMap& texture = material_slot.textures[texture_slot];
								ImGui::Text(editor_text::texture_status_format, texture_slot_names[texture_slot], texture.IsValid() ? editor_text::assigned : editor_text::none);
							}

							if (material_changed)
							{
								material_comp->SetDirty();
							}
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							EditorViewport::DeferredResRemoval deferred_res_removal = {};
							deferred_res_removal.frames_left = 8;

							ecs::MaterialComponent* material = editor_viewport.view->scene->GetComponent<ecs::MaterialComponent>(entity);
							if (material && material->material)
							{
								for (resource::MaterialSlot& material_slot : material->material->slots)
								{
									for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
									{
										if (material_slot.textures[texture_slot].texture)
										{
											deferred_res_removal.resources.push_back(material_slot.textures[texture_slot].texture);
										}
									}
								}
							}

							if (!deferred_res_removal.meshes.empty() || !deferred_res_removal.resources.empty())
							{
								editor_viewport.deferred_res_removals.push_back(std::move(deferred_res_removal));
							}

							editor_viewport.view->scene->RemoveComponent<MaterialComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				ScriptComponent* script_comp = editor_viewport.view->scene->GetComponent<ScriptComponent>(editor_viewport.picked_entity);
				if (script_comp)
				{
					ImGui::PushID("ScriptComponent");
					ImGui::Text(editor_text::script_component);
					bool remove_component = DrawComponentRemoveButton(editor_text::script_component);
					auto reload_script = [this](ecs::Entity entity, ScriptSlot& script_slot)
						{
							if (!script_runtime || script_slot.script_path.empty())
							{
								return;
							}

							script::ScriptCallContext context = {};
							context.scene = editor_viewport.view->scene;
							context.entity = entity;

							script::ScriptInstanceDesc desc = {};
							desc.script_path = script_slot.script_path;

							if (script_slot.instance.IsValid())
							{
								script::ScriptCallDesc call_desc = {};
								call_desc.type = script::ScriptCallType::OnDestroy;
								call_desc.context = context;
								script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error);
								script_runtime->DestroyInstance(script_slot.instance);
								script_slot.instance = {};
							}

							script_runtime->ReloadScript(script_slot.script_path, script_slot.last_error);
							if (!script_runtime->CreateInstance(desc, script_slot.instance, script_slot.last_error))
							{
								script_slot.initialized = false;
								return;
							}

							script_slot.initialized = false;
							script::ScriptCallDesc call_desc = {};
							call_desc.type = script::ScriptCallType::OnCreate;
							call_desc.context = context;
							if (script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error))
							{
								script_slot.initialized = true;
							}
						};

					if (!remove_component)
					{
						bool enabled = script_comp->enabled;
						if (ImGui::Checkbox(editor_text::enabled, &enabled))
						{
							script_comp->enabled = enabled;
						}

						for (Size script_index = 0; script_index < script_comp->scripts.size();)
						{
							ScriptSlot& script_slot = script_comp->scripts[script_index];
							ImGui::PushID(static_cast<int>(script_index));

							bool slot_enabled = script_slot.enabled;
							if (ImGui::Checkbox(editor_text::script_enabled, &slot_enabled))
							{
								script_slot.enabled = slot_enabled;
							}

							String script_label = editor_text::none_placeholder;
							if (!script_slot.script_path.empty())
							{
								script_label = script_slot.script_path.rfind(project::content_virtual_root_prefix, 0) == 0 ? script_slot.script_path : "/Contents/" + script_slot.script_path;
							}

							ImGui::SetNextItemWidth(-1.0f);
							if (ImGui::BeginCombo(editor_text::script, script_label.c_str()))
							{
								for (const ContentBrowserAsset& asset : content_browser.assets)
								{
									if (asset.type != ContentAssetType::Script)
									{
										continue;
									}

									const String asset_script_path = io::GetRelativePath(contents_root_dir, asset.disk_path);
									if (asset_script_path.empty())
									{
										continue;
									}

									const bool selected = asset_script_path == script_slot.script_path;
									if (ImGui::Selectable(asset.virtual_path.c_str(), selected))
									{
										if (asset_script_path == script_slot.script_path || !HasScript(*script_comp, asset_script_path))
										{
											if (asset_script_path != script_slot.script_path && script_runtime && script_slot.instance.IsValid())
											{
												script::ScriptCallContext context = {};
												context.scene = editor_viewport.view->scene;
												context.entity = editor_viewport.picked_entity;
												script::ScriptCallDesc call_desc = {};
												call_desc.type = script::ScriptCallType::OnDestroy;
												call_desc.context = context;
												script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error);
												script_runtime->DestroyInstance(script_slot.instance);
												script_slot.instance = {};
												script_slot.initialized = false;
											}

											script_slot.script_path = asset_script_path;
											script_slot.last_error.clear();
										}
										else
										{
											script_slot.last_error = editor_text::script_already_exists;
										}
									}

									if (selected)
									{
										ImGui::SetItemDefaultFocus();
									}
								}
								ImGui::EndCombo();
							}

							if (ImGui::BeginDragDropTarget())
							{
								if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CONTENT_ASSET_PATH"))
								{
									String dropped_path(static_cast<const char*>(payload->Data));
									if (won::utils::ToLower(io::GetExtension(dropped_path)) == "lua")
									{
										const String dropped_script_path = io::GetRelativePath(contents_root_dir, dropped_path);
										if (!dropped_script_path.empty() && (dropped_script_path == script_slot.script_path || !HasScript(*script_comp, dropped_script_path)))
										{
											if (dropped_script_path != script_slot.script_path && script_runtime && script_slot.instance.IsValid())
											{
												script::ScriptCallContext context = {};
												context.scene = editor_viewport.view->scene;
												context.entity = editor_viewport.picked_entity;
												script::ScriptCallDesc call_desc = {};
												call_desc.type = script::ScriptCallType::OnDestroy;
												call_desc.context = context;
												script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error);
												script_runtime->DestroyInstance(script_slot.instance);
												script_slot.instance = {};
												script_slot.initialized = false;
											}

											script_slot.script_path = dropped_script_path;
											script_slot.last_error.clear();
										}
										else if (!dropped_script_path.empty())
										{
											script_slot.last_error = editor_text::script_already_exists;
										}
									}
								}
								ImGui::EndDragDropTarget();
							}

							char path_buf[512];
							std::snprintf(path_buf, sizeof(path_buf), "%s", script_slot.script_path.c_str());
							if (ImGui::InputText(editor_text::path, path_buf, sizeof(path_buf)))
							{
								String new_path = io::NormalizePath(path_buf);
								if (new_path.rfind(project::content_virtual_root_prefix, 0) == 0)
								{
									new_path = io::NormalizePath(new_path.substr(project::content_virtual_root_prefix_length));
								}
								else if (io::IsAbsolutePath(new_path))
								{
									const String relative_path = io::GetRelativePath(contents_root_dir, new_path);
									if (!relative_path.empty())
									{
										new_path = relative_path;
									}
									else
									{
										new_path = script_slot.script_path;
									}
								}
								if (new_path.empty() || new_path == script_slot.script_path || !HasScript(*script_comp, new_path))
								{
									if (new_path != script_slot.script_path && script_runtime && script_slot.instance.IsValid())
									{
										script::ScriptCallContext context = {};
										context.scene = editor_viewport.view->scene;
										context.entity = editor_viewport.picked_entity;
										script::ScriptCallDesc call_desc = {};
										call_desc.type = script::ScriptCallType::OnDestroy;
										call_desc.context = context;
										script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error);
										script_runtime->DestroyInstance(script_slot.instance);
										script_slot.instance = {};
										script_slot.initialized = false;
									}

									script_slot.script_path = new_path;
									script_slot.last_error.clear();
								}
								else
								{
									script_slot.last_error = editor_text::script_already_exists;
								}
							}

							if (ImGui::Button(editor_text::reload, ImVec2(DEFAULTBUTTONWIDTH, 0)))
							{
								reload_script(editor_viewport.picked_entity, script_slot);
							}

							ImGui::SameLine();
							const bool remove_script = ImGui::Button(editor_text::remove, ImVec2(DEFAULTBUTTONWIDTH, 0));
							ImGui::SameLine();
							if (script_index == 0)
							{
								ImGui::BeginDisabled();
							}
							const bool move_up = ImGui::Button(editor_text::up, ImVec2(DEFAULTBUTTONWIDTH, 0));
							if (script_index == 0)
							{
								ImGui::EndDisabled();
							}
							ImGui::SameLine();
							if (script_index + 1 >= script_comp->scripts.size())
							{
								ImGui::BeginDisabled();
							}
							const bool move_down = ImGui::Button(editor_text::down, ImVec2(DEFAULTBUTTONWIDTH, 0));
							if (script_index + 1 >= script_comp->scripts.size())
							{
								ImGui::EndDisabled();
							}

							if (!script_slot.last_error.empty())
							{
								ImGui::TextWrapped(editor_text::last_error_format, script_slot.last_error.c_str());
							}

							ImGui::PopID();

							if (remove_script)
							{
								if (script_runtime && script_slot.instance.IsValid())
								{
									script::ScriptCallContext context = {};
									context.scene = editor_viewport.view->scene;
									context.entity = editor_viewport.picked_entity;
									script::ScriptCallDesc call_desc = {};
									call_desc.type = script::ScriptCallType::OnDestroy;
									call_desc.context = context;
									script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error);
									script_runtime->DestroyInstance(script_slot.instance);
								}

								script_comp->scripts.erase(script_comp->scripts.begin() + script_index);
								continue;
							}

							if (move_up && script_index > 0)
							{
								std::swap(script_comp->scripts[script_index], script_comp->scripts[script_index - 1]);
								++script_index;
								continue;
							}

							if (move_down && script_index + 1 < script_comp->scripts.size())
							{
								std::swap(script_comp->scripts[script_index], script_comp->scripts[script_index + 1]);
								++script_index;
								continue;
							}

							++script_index;
						}

						if (ImGui::Button(editor_text::add_script, ImVec2(DEFAULTBUTTONWIDTH, 0)))
						{
							script_comp->scripts.push_back({});
						}
					}
					else
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							ScriptComponent* script = editor_viewport.view->scene->GetComponent<ScriptComponent>(entity);
							if (script && script_runtime)
							{
								script::ScriptCallContext context = {};
								context.scene = editor_viewport.view->scene;
								context.entity = entity;
								for (ScriptSlot& script_slot : script->scripts)
								{
									if (script_slot.instance.IsValid())
									{
										script::ScriptCallDesc call_desc = {};
										call_desc.type = script::ScriptCallType::OnDestroy;
										call_desc.context = context;
										script_runtime->Call(script_slot.instance, call_desc, script_slot.last_error);
										script_runtime->DestroyInstance(script_slot.instance);
									}
								}
							}
							editor_viewport.view->scene->RemoveComponent<ScriptComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				for (const won::TypeDesc* type_desc : editor_viewport.view->scene->GetComponentTypes())
				{
					if (!type_desc || !editor_viewport.view->scene->HasComponent(editor_viewport.picked_entity, type_desc->type_id))
					{
						continue;
					}

					if (DrawReflectedComponent(*editor_viewport.view->scene, editor_viewport.picked_entity, type_desc))
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						const won::TypeId type_id = type_desc->type_id;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity, type_id](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent(entity, type_id);
						});
					}
					else if (type_desc->type_id == reflection::TypeMeta<TerrainComponent>::type_id)
					{
						// Authoring-time terrain bake: generate the mesh from the recipe, save it as a
						// .wonmesh asset, and point the GeometryComponent at it. Requires Geometry on the entity.
						ImGui::PushID("TerrainRegenerate");
						TerrainComponent* terrain = editor_viewport.view->scene->GetComponent<TerrainComponent>(editor_viewport.picked_entity);
						if (ImGui::Button(editor_text::regenerate_terrain))
						{
							GeometryComponent* geometry = editor_viewport.view->scene->GetComponent<GeometryComponent>(editor_viewport.picked_entity);
							if (terrain && geometry && device)
							{
								auto terrain_mesh = GenerateTerrainMesh(*terrain);
								if (terrain_mesh && terrain_mesh->IsValid())
								{
									const String terrain_rel_path = String(generated_asset_directory) + "/terrain_" + std::to_string(editor_viewport.picked_entity) + "." + resource::mesh_binary_extension;
									const String terrain_full_path = io::CombinePath(contents_root_dir, terrain_rel_path);
									io::CreateDirectories(io::GetDirectoryFromPath(terrain_full_path));
									if (resource::SaveMeshBinary(terrain_full_path, *terrain_mesh))
									{
										// Swap the mesh at a thread-safe point and defer the old mesh's GPU
										// resources, so in-flight command lists do not reference freed buffers.
										const ecs::Entity terrain_entity = editor_viewport.picked_entity;
										eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, terrain_entity, terrain_mesh, terrain_rel_path](const won::function::Value&) {
											GeometryComponent* geom = editor_viewport.view->scene->GetComponent<GeometryComponent>(terrain_entity);
											if (!geom || !device || !rendering::utils::CreateRenderData(*device, *terrain_mesh))
											{
												return;
											}
											if (geom->mesh && geom->mesh != terrain_mesh)
											{
												EditorViewport::DeferredResRemoval deferred_res_removal = {};
												deferred_res_removal.frames_left = 8;
												deferred_res_removal.meshes.push_back(geom->mesh);
												if (geom->mesh->render_data.buffer)
												{
													deferred_res_removal.resources.push_back(geom->mesh->render_data.buffer);
												}
												if (geom->mesh->gpu_bvh.node_buffer)
												{
													deferred_res_removal.resources.push_back(geom->mesh->gpu_bvh.node_buffer);
												}
												if (geom->mesh->gpu_bvh.primitive_buffer)
												{
													deferred_res_removal.resources.push_back(geom->mesh->gpu_bvh.primitive_buffer);
												}
												editor_viewport.deferred_res_removals.push_back(std::move(deferred_res_removal));
											}
											geom->mesh_asset_path = terrain_rel_path;
											geom->SetMesh(terrain_mesh);
											editor_viewport.view->scene->SetBVHDirty();
										});
									}
								}
							}
						}
						ImGui::PopID();
					}
				}

				if (ImGui::Button(editor_text::add_component, ImVec2(-1.0f, 0.0f)))
				{
					ImGui::OpenPopup(editor_text::add_component_popup);
				}

				if (ImGui::BeginPopup(editor_text::add_component_popup))
				{
					Vector<const won::TypeDesc*> component_types = editor_viewport.view->scene->GetComponentTypes();
					std::sort(component_types.begin(), component_types.end(), [](const won::TypeDesc* lhs, const won::TypeDesc* rhs) {
						return StringView(GetTypeDisplayName(lhs)) < StringView(GetTypeDisplayName(rhs));
					});

					bool has_component_item = false;
					for (const won::TypeDesc* type_desc : component_types)
					{
						if (!type_desc || editor_viewport.view->scene->HasComponent(editor_viewport.picked_entity, type_desc->type_id))
						{
							continue;
						}

						if (ImGui::MenuItem(GetTypeDisplayName(type_desc)))
						{
							void* component = editor_viewport.view->scene->AddComponent(editor_viewport.picked_entity, type_desc);
							if (component && type_desc->type_id == reflection::TypeMeta<NameComponent>::type_id)
							{
								static_cast<NameComponent*>(component)->value = "Entity " + std::to_string(editor_viewport.picked_entity);
							}
						}
						has_component_item = true;
					}
					if (!has_component_item)
					{
						ImGui::TextDisabled(editor_text::no_components_available);
					}

					ImGui::EndPopup();
				}
			}
		}
		ImGui::End();

		if (ImGui::Begin(editor_text::contents_browser_window, nullptr))
		{
			DrawContentsBrowser();
		}
		ImGui::End();

		if (ImGui::Begin(editor_text::log_window, nullptr, ImGuiWindowFlags_NoScrollbar))
		{
			static std::string lastlog = "";

			//ImGui::SameLine();
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));
			if (ImGui::Button(editor_text::copy_to_clipboard, ImVec2(DEFAULTBUTTONWIDTH, 0)))
				ImGui::SetClipboardText(lastlog.c_str());
			ImGui::SameLine();
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));
			if (ImGui::Button(editor_text::clear, ImVec2(DEFAULTBUTTONWIDTH, 0)))
				lastlog.clear();

			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, 3));

			std::string log = won::backlog::GetText();
			if (log.size() > 0)
			{
				lastlog += log;
				won::backlog::Clear();
			}

			ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			ImGui::Text("%s", lastlog.c_str());

			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();

		}
		ImGui::End();

		if (ImGui::Begin(editor_text::profiler_window, nullptr, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));

			static bool profiler_enabled = false;
			static double last_profiler_ui_update_time = -1.0;
			static std::string cached_performance;
			static std::string cached_res_usage;
			if (ImGui::Checkbox(editor_text::enable_profiler, &profiler_enabled))
			{
				profiler::SetEnabled(profiler_enabled);
				last_profiler_ui_update_time = -1.0;
				cached_performance = profiler_enabled ? editor_text::profiler_starting : "";
				cached_res_usage.clear();
			}

			if (profiler_enabled)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), editor_text::profiler_warning);

				ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, 3));

				constexpr double profiler_ui_update_interval = 0.5;
				const double profiler_ui_time = ImGui::GetTime();
				if (profiler::IsEnabled() && (last_profiler_ui_update_time < 0.0 || profiler_ui_time - last_profiler_ui_update_time >= profiler_ui_update_interval))
				{
					profiler::GetProfileInfo(cached_performance, cached_res_usage);
					last_profiler_ui_update_time = profiler_ui_time;
				}

				std::string profile = cached_performance + "\n" + cached_res_usage;
				ImGui::BeginChild("##Profiler", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
				ImGui::Text("%s", profile.c_str());

				if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
					ImGui::SetScrollHereY(1.0f);
				ImGui::EndChild();
			}
		}
		ImGui::End();

		if (focus_contents_browser_on_startup)
		{
			ImGui::SetWindowFocus(editor_text::contents_browser_window);
			focus_contents_browser_on_startup = false;
		}

		// Rendering
		ImGui::Render();

		auto drawData = ImGui::GetDrawData();

		if (!drawData || drawData->TotalVtxCount == 0)
		{
			return;
		}

		// Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
		int fb_width = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
		int fb_height = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
		if (fb_width <= 0 || fb_height <= 0)
			return;

		DrawEditorGrid();

		Renderer::FrameContext& frame_context = renderer->GetFrameContext();
		RHICommandList* command_list = frame_context.BeginCommandList(*device);
		if (!command_list)
		{
			return;
		}

		jobsystem::Execute(renderer->GetRenderingWorkContext(), [this, drawData, fb_width, fb_height, command_list](jobsystem::JobArgs args) {

			Renderer::FrameContext& frame_context = renderer->GetFrameContext();
			RHISubresourceBinding back_buffer_binding = {};
			if (!renderer->GetCurrentBackBufferBinding(back_buffer_binding))
			{
				return;
			}
			Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };

			Renderer::FrameUploadAllocation allocation{};
			auto gpu_range = profiler::ScopedRangeGPU("ImGui", *command_list);

			// Setup orthographic projection matrix into our constant buffer
			struct ImGuiConstants
			{
				float   mvp[4][4];
			};

			// Get memory for vertex and index buffers
			const uint64_t vbSize = sizeof(ImDrawVert) * drawData->TotalVtxCount;
			const uint64_t ibSize = sizeof(ImDrawIdx) * drawData->TotalIdxCount;
			const uint64_t cbSize = sizeof(ImGuiConstants);
			const uint64_t totalSize = vbSize + ibSize + cbSize;

			RHIBufferDesc buffer_desc;
			buffer_desc.bind_flags = RHIBindFlags::VertexBuffer | RHIBindFlags::IndexBuffer | RHIBindFlags::ConstantBuffer;
			auto buffer_align = device->GetMinOffsetAlignment(buffer_desc);
			if (!frame_context.AllocateFrameUpload(*device, totalSize, buffer_align, allocation))
			{
				return;
			}

			RHIViewport viewport;
			viewport.width = (float)fb_width;
			viewport.height = (float)fb_height;
			command_list->SetRenderTargets(color_targets, nullptr);
			command_list->SetViewport(viewport);
			command_list->SetGraphicsPipeline(*imgui_pso);
			command_list->SetSampler(RHIShaderStage::Pixel, 0, *imgui_sampler);

			const Size cb_data_offset = 0;
			const Size vb_data_offset = cb_data_offset + cbSize;
			const Size ib_data_offset = vb_data_offset + vbSize;
			const Size cb_buffer_offset = allocation.buffer_offset + cb_data_offset;
			const Size vb_buffer_offset = allocation.buffer_offset + vb_data_offset;
			const Size ib_buffer_offset = allocation.buffer_offset + ib_data_offset;
			uint8* allocation_base = static_cast<uint8*>(allocation.mapped_data);

			// Copy and convert all vertices into a single contiguous buffer
			ImDrawVert* vertexCPUMem = reinterpret_cast<ImDrawVert*>(allocation_base + vb_data_offset);
			ImDrawIdx* indexCPUMem = reinterpret_cast<ImDrawIdx*>(allocation_base + ib_data_offset);
			for (int cmdListIdx = 0; cmdListIdx < drawData->CmdListsCount; cmdListIdx++)
			{
				const ImDrawList* drawList = drawData->CmdLists[cmdListIdx];
				memcpy(vertexCPUMem, &drawList->VtxBuffer[0], drawList->VtxBuffer.Size * sizeof(ImDrawVert));
				memcpy(indexCPUMem, &drawList->IdxBuffer[0], drawList->IdxBuffer.Size * sizeof(ImDrawIdx));
				vertexCPUMem += drawList->VtxBuffer.Size;
				indexCPUMem += drawList->IdxBuffer.Size;
			}

			{
				const float L = drawData->DisplayPos.x;
				const float R = drawData->DisplayPos.x + drawData->DisplaySize.x;
				const float T = drawData->DisplayPos.y;
				const float B = drawData->DisplayPos.y + drawData->DisplaySize.y;

				float mvp[4][4] =
				{
					{ 2.0f / (R - L),   0.0f,           0.0f,       0.0f },
					{ 0.0f,         2.0f / (T - B),     0.0f,       0.0f },
					{ 0.0f,         0.0f,           0.5f,       0.0f },
					{ (R + L) / (L - R),  (T + B) / (B - T),    0.5f,       1.0f },
				};
				std::memcpy(allocation_base + cb_data_offset, mvp, sizeof(mvp));

				RHISubresourceHandle cb_subresource_handle{};

				RHISubresourceDesc subresource_desc{};
				subresource_desc.type = RHISubresourceType::ConstantBuffer;
				subresource_desc.buffer_offset = cb_buffer_offset;
				subresource_desc.buffer_size = cbSize;
				subresource_desc.buffer_stride = sizeof(ImGuiConstants);
				device->CreateSubresource(*allocation.buffer, subresource_desc, &cb_subresource_handle);

				RHISubresourceBinding cb_binding;
				cb_binding.resource = allocation.buffer.get();
				cb_binding.subresource = cb_subresource_handle;

				command_list->SetVertexBuffer(*allocation.buffer, sizeof(ImDrawVert), vb_buffer_offset, vbSize);
				command_list->SetIndexBuffer(*allocation.buffer, sizeof(ImDrawIdx), ib_buffer_offset, ibSize);
				command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, cb_binding);
			}

			// Will project scissor/clipping rectangles into framebuffer space
			ImVec2 clip_off = drawData->DisplayPos;         // (0,0) unless using multi-viewports
			ImVec2 clip_scale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

			//passEncoder->SetSampler(0, Sampler::LinearWrap());

			// Render command lists
			int32_t vertexOffset = 0;
			uint32_t indexOffset = 0;
			command_list->SetPrimitiveTopology(RHIPrimitiveTopology::TriangleList);
			for (uint32_t cmdListIdx = 0; cmdListIdx < (uint32_t)drawData->CmdListsCount; ++cmdListIdx)
			{
				const ImDrawList* drawList = drawData->CmdLists[cmdListIdx];
				for (uint32_t cmdIndex = 0; cmdIndex < (uint32_t)drawList->CmdBuffer.size(); ++cmdIndex)
				{
					const ImDrawCmd* drawCmd = &drawList->CmdBuffer[cmdIndex];
					if (drawCmd->UserCallback)
					{
						// User callback, registered via ImDrawList::AddCallback()
						// (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
						if (drawCmd->UserCallback == ImDrawCallback_ResetRenderState)
						{
						}
						else
						{
							drawCmd->UserCallback(drawList, drawCmd);
						}
					}
					else
					{
						// Project scissor/clipping rectangles into framebuffer space
						ImVec2 clip_min(drawCmd->ClipRect.x - clip_off.x, drawCmd->ClipRect.y - clip_off.y);
						ImVec2 clip_max(drawCmd->ClipRect.z - clip_off.x, drawCmd->ClipRect.w - clip_off.y);
						if (clip_max.x < clip_min.x || clip_max.y < clip_min.y)
							continue;

						// Apply scissor/clipping rectangle
						RHIRect scissor;
						scissor.x = (int32)(clip_min.x);
						scissor.y = (int32)(clip_min.y);
						scissor.width = (int32)(clip_max.x - clip_min.x);
						scissor.height = (int32)(clip_max.y - clip_min.y);
						command_list->SetScissor(scissor);

						const RHIResource* texture = (const RHIResource*)drawCmd->GetTexID();
						RHISubresourceBinding binding;
						binding.resource = imgui_font.get();
						binding.subresource = imgui_font_subresource;
						command_list->SetShaderResource(RHIShaderStage::Pixel, 0, binding);
						command_list->DrawIndexed(drawCmd->ElemCount, 1, indexOffset + drawCmd->IdxOffset, vertexOffset + drawCmd->VtxOffset, 0);
					}
				}
				indexOffset += drawList->IdxBuffer.size();
				vertexOffset += drawList->VtxBuffer.size();
			}

			//// Restore Scissor
			//{
			//	RHIRect scissor;
			//	scissor.x = 0;
			//	scissor.y = 0;
			//	scissor.width = (int32_t)viewport.width;
			//	scissor.height = (int32_t)viewport.height;
			//	command_list->SetScissor(scissor);
			//}
		});
	}

	void EditorApplication::InitImGui()
	{
		// Build texture atlas
		ImGuiIO& io = ImGui::GetIO();

		unsigned char* pixels;
		int width, height;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		// Upload texture to graphics system
		rendering::RHITextureDesc texture_desc;
		texture_desc.width = width;
		texture_desc.height = height;
		texture_desc.mip_levels = 1;
		texture_desc.array_layers = 1;
		texture_desc.format = RHIFormat::R8G8B8A8Unorm;
		texture_desc.bind_flags = RHIBindFlags::ShaderResource;

		imgui_font = device->CreateTexture(texture_desc, (void*)pixels, width * height * 4);
		RHISubresourceDesc subresource_desc;
		subresource_desc.type = RHISubresourceType::ShaderResource;
		device->CreateSubresource(*imgui_font, subresource_desc, &imgui_font_subresource);

		RHISamplerDesc sampler_desc;
		sampler_desc.address_u = RHIAddressMode::Wrap;
		sampler_desc.address_v = RHIAddressMode::Wrap;
		sampler_desc.address_w = RHIAddressMode::Wrap;
		imgui_sampler = device->CreateSampler(sampler_desc);

		// Store our identifier
		io.Fonts->SetTexID((ImTextureID)imgui_font.get());

		// Create pipeline
		RHIGraphicsPipelineDesc pipeline_desc{};
		pipeline_desc.vertex_shader = &imgui_vs;
		pipeline_desc.pixel_shader = &imgui_ps;
		pipeline_desc.input_layout.push_back({ "POSITION", 0, RHIFormat::R32G32Float, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, pos), false, 0 });
		pipeline_desc.input_layout.push_back({ "TEXCOORD", 0, RHIFormat::R32G32Float, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, uv), false, 0 });
		pipeline_desc.input_layout.push_back({ "COLOR", 0, RHIFormat::R8G8B8A8Unorm, 0, (uint32_t)IM_OFFSETOF(ImDrawVert, col), false, 0 });
		pipeline_desc.depth_stencil_format = RHIFormat::Unknown;
		pipeline_desc.depth_stencil.depth_test = false;
		pipeline_desc.depth_stencil.depth_write = false;
		pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
		pipeline_desc.render_target_formats = { RHIFormat::R8G8B8A8Unorm };
		pipeline_desc.raster.cull_mode = RHICullMode::None;
		pipeline_desc.blend.enable = true;
		pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;

		imgui_pso = device->CreateGraphicsPipeline(pipeline_desc);

		return;
	}

	void EditorApplication::InitEditorGrid()
	{
		RHIGraphicsPipelineDesc pipeline_desc = {};
		pipeline_desc.vertex_shader = &editor_grid_vs;
		pipeline_desc.pixel_shader = &editor_grid_ps;
		pipeline_desc.depth_stencil.depth_test = true;
		pipeline_desc.depth_stencil.depth_write = false;
		pipeline_desc.depth_stencil.depth_compare = RHICompareOp::GreaterEqual;
		pipeline_desc.render_target_formats = { RENDERTARGET_BUFFER_FORMAT };
		pipeline_desc.depth_stencil_format = DEPTH_BUFFER_FORMAT;
		pipeline_desc.raster.cull_mode = RHICullMode::None;
		pipeline_desc.blend.enable = true;
		pipeline_desc.topology = RHIPrimitiveTopology::TriangleList;

		editor_grid_pso = device->CreateGraphicsPipeline(pipeline_desc);
	}

	void EditorApplication::CreateEditorCamera()
	{
		if (!editor_viewport.view || !editor_viewport.view->scene)
		{
			return;
		}

		editor_viewport.view->camera_entity = editor_viewport.view->scene->CreateEntity();
		auto camera_transform = editor_viewport.view->scene->AddComponent<ecs::TransformComponent>(editor_viewport.view->camera_entity);
		if (camera_transform)
		{
			camera_transform->position = { 0.0f, 0.0f, 0.0f };
			camera_transform->rotation = { 0.0f, 0.0f, 0.0f, 1.0f };
			camera_transform->SetDirty();
		}

		auto camera = editor_viewport.view->scene->AddComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity);
		if (camera)
		{
			float viewport_width = static_cast<float>(editor_viewport.view->viewport.width);
			float viewport_height = static_cast<float>(editor_viewport.view->viewport.height);
			if (viewport_height <= 0.0f)
			{
				viewport_height = 1.0f;
			}
			camera->SetAspectRatio(viewport_width / viewport_height);
			camera->SetNearFar(0.1f, 1000.0f);
			camera->SetFOV_Y(math::PI / 3.0f);
			camera->SetOrtho(false);
		}

		if (auto name = editor_viewport.view->scene->AddComponent<ecs::NameComponent>(editor_viewport.view->camera_entity))
		{
			name->value = "Editor Camera";
		}
	}

	void EditorApplication::CreateStartupScene()
	{
		if (!editor_viewport.view || !editor_viewport.view->scene)
		{
			return;
		}

		ecs::Entity environment_entity = editor_viewport.view->scene->CreateEntity();
		if (auto environment = editor_viewport.view->scene->AddComponent<ecs::EnvironmentComponent>(environment_entity))
		{
			environment->SetActive(true);
		}
		if (auto name = editor_viewport.view->scene->AddComponent<ecs::NameComponent>(environment_entity))
		{
			name->value = "Editor Environment";
		}

		CreateEditorCamera();
	}

	bool EditorApplication::NewProject(const String& path)
	{
		if (path.empty())
		{
			return false;
		}

		String settings_path = io::NormalizePath(path);
		if (io::GetExtension(settings_path) != project::project_file_extension)
		{
			settings_path = io::ReplaceExtension(settings_path, project::project_file_extension);
		}

		String project_root = io::NormalizePath(io::GetDirectoryFromPath(settings_path));
		if (project_root.empty() || !io::CreateDirectories(project_root))
		{
			backlog::Post(editor_text::create_project_failed + project_root, backlog::LogLevel::Warning);
			return false;
		}

		project::ProjectSettings new_project_settings = {};
		new_project_settings.settings_path = settings_path;
		new_project_settings.project_root = project_root;
		new_project_settings.project_name = io::ReplaceExtension(io::GetFilename(settings_path), "");
		if (new_project_settings.project_name.empty())
		{
			new_project_settings.project_name = "NewProject";
		}
		new_project_settings.content_root = "Contents";
		new_project_settings.startup_scene = String(scene_directory_name) + "/" + default_scene_file_name + "." + resource::scene_file_extension;
		new_project_settings.window_title = new_project_settings.project_name;
		new_project_settings.splash_title = new_project_settings.project_name;

		const String new_content_root = project::GetContentRoot(new_project_settings);
		if (!io::CreateDirectories(new_content_root) ||
			!io::CreateDirectories(io::CombinePath(new_content_root, scene_directory_name)) ||
			!io::CreateDirectories(io::CombinePath(new_content_root, "Images")))
		{
			backlog::Post(editor_text::create_project_failed + new_content_root, backlog::LogLevel::Warning);
			return false;
		}

		if (!project::SaveSettings(settings_path, new_project_settings))
		{
			backlog::Post(editor_text::create_project_failed + settings_path, backlog::LogLevel::Warning);
			return false;
		}

		if (!LoadProject(settings_path))
		{
			return false;
		}

		const String startup_scene_path = project::ResolveProjectContentPath(contents_root_dir, loaded_project_settings.startup_scene);
		if (!SaveScene(startup_scene_path))
		{
			return false;
		}
		SaveProject();
		backlog::Post(editor_text::project_created + settings_path);
		return true;
	}

	bool EditorApplication::LoadProject(const String& path)
	{
		if (path.empty())
		{
			return false;
		}

		project::ProjectSettings project_settings_to_load = {};
		const String settings_path = io::NormalizePath(path);
		if (!io::IsFile(settings_path) || !project::LoadSettings(settings_path, project_settings_to_load))
		{
			backlog::Post(editor_text::load_project_failed + settings_path, backlog::LogLevel::Warning);
			return false;
		}

		for (const std::shared_ptr<EditorAssetImporter::ImportTask>& task : asset_importer.tasks)
		{
			if (!task)
			{
				continue;
			}

			jobsystem::Wait(task->context);
		}
		asset_importer.tasks.clear();

		WaitIdle();
		loaded_project_settings = project_settings_to_load;
		contents_root_dir = project::GetContentRoot(loaded_project_settings);
		contents_root_dir = io::NormalizePath(contents_root_dir);
		if (!contents_root_dir.empty() && contents_root_dir.back() != '/')
		{
			contents_root_dir += "/";
		}

		ecs::SceneDesc scene_desc = {};
		scene_desc.script_runtime = script_runtime.get();
		scene_desc.physics = project::GetPhysicsDesc(loaded_project_settings);
		scene_desc.audio_mixer = audio_mixer.get();
		loaded_scene = ecs::Scene(scene_desc);
		if (editor_viewport.view)
		{
			editor_viewport.view->scene = &loaded_scene;
			editor_viewport.view->camera_entity = ecs::INVALID_ENTITY;
		}
		current_scene_path.clear();
		editor_viewport.picked_entity = ecs::INVALID_ENTITY;
		editor_viewport.debug_primitive_entity = ecs::INVALID_ENTITY;
		editor_viewport.debug_primitive_mesh.reset();
		editor_viewport.deferred_res_removals.clear();
		editor_viewport.camera_controller = {};
		plugins.clear();
		LoadPlugins();

		bool startup_scene_loaded = false;
		String startup_scene_path = loaded_project_settings.startup_scene;
		if (!startup_scene_path.empty())
		{
			startup_scene_path = project::ResolveProjectContentPath(contents_root_dir, startup_scene_path);
			if (io::IsFile(startup_scene_path))
			{
				LoadScene(startup_scene_path);
				startup_scene_loaded = true;
			}
		}
		if (!startup_scene_loaded)
		{
			CreateStartupScene();
		}

		content_browser.current_folder = project::content_virtual_root;
		content_browser.search[0] = '\0';
		content_browser.initialized = false;
		contents_watcher = io::CreateDirectoryWatcher(contents_root_dir, true);
		contents_watcher_poll_timer = 0.0f;
		game_data_editor = {};
		RebuildContentBrowser();
		UpdateEntityList();
		backlog::Post(editor_text::project_loaded + settings_path);
		return true;
	}

	bool EditorApplication::SaveProject()
	{
		if (loaded_project_settings.settings_path.empty())
		{
			backlog::Post(editor_text::save_project_failed + String(editor_text::package_project_missing_settings), backlog::LogLevel::Warning);
			return false;
		}

		const String settings_directory = io::GetDirectoryFromPath(loaded_project_settings.settings_path);
		if (!settings_directory.empty() && !io::CreateDirectories(settings_directory))
		{
			backlog::Post(editor_text::save_project_failed + settings_directory, backlog::LogLevel::Warning);
			return false;
		}

		if (!project::SaveSettings(loaded_project_settings.settings_path, loaded_project_settings))
		{
			backlog::Post(editor_text::save_project_failed + loaded_project_settings.settings_path, backlog::LogLevel::Warning);
			return false;
		}

		String saved_content_root = project::GetContentRoot(loaded_project_settings);
		saved_content_root = io::NormalizePath(saved_content_root);
		if (!saved_content_root.empty() && saved_content_root.back() != '/')
		{
			saved_content_root += "/";
		}
		if (saved_content_root != contents_root_dir)
		{
			contents_root_dir = saved_content_root;
			io::CreateDirectories(contents_root_dir);
			content_browser.current_folder = project::content_virtual_root;
			content_browser.initialized = false;
			contents_watcher = io::CreateDirectoryWatcher(contents_root_dir, true);
			contents_watcher_poll_timer = 0.0f;
			RebuildContentBrowser();
		}

		backlog::Post(editor_text::project_saved + loaded_project_settings.settings_path);
		return true;
	}

	void EditorApplication::DrawProjectSettingsWindow(bool* open)
	{
		if (!open || !*open)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(editor_text::project_settings, open))
		{
			auto draw_label = [](const char* label)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0);
				ImGui::TextUnformatted(label);
				ImGui::TableSetColumnIndex(1);
				ImGui::SetNextItemWidth(-1.0f);
			};

			auto draw_string_field = [](const char* label, String& value) -> bool
			{
				char buffer[1024] = {};
				strncpy_s(buffer, value.c_str(), sizeof(buffer) - 1);
				String id = "##";
				id += label;
				if (ImGui::InputText(id.c_str(), buffer, sizeof(buffer)))
				{
					value = buffer;
					return true;
				}
				return false;
			};

			if (ImGui::BeginTable("ProjectSettingsTable", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				draw_label("Project Name");
				draw_string_field("Project Name", loaded_project_settings.project_name);
				draw_label("Content Root");
				draw_string_field("Content Root", loaded_project_settings.content_root);
				draw_label("Startup Scene");
				draw_string_field("Startup Scene", loaded_project_settings.startup_scene);
				draw_label("Window Title");
				draw_string_field("Window Title", loaded_project_settings.window_title);
				draw_label("Window Width");
				ImGui::InputInt("##Window Width", &loaded_project_settings.window_width);
				loaded_project_settings.window_width = (std::max)(1, loaded_project_settings.window_width);
				draw_label("Window Height");
				ImGui::InputInt("##Window Height", &loaded_project_settings.window_height);
				loaded_project_settings.window_height = (std::max)(1, loaded_project_settings.window_height);
				draw_label("Fullscreen");
				ImGui::Checkbox("##Fullscreen", &loaded_project_settings.window_fullscreen);
				draw_label("Resizable");
				ImGui::Checkbox("##Resizable", &loaded_project_settings.window_resizable);
				draw_label("Use Title Bar");
				ImGui::Checkbox("##Use Title Bar", &loaded_project_settings.window_use_title_bar);
				draw_label("Window Visible");
				ImGui::Checkbox("##Window Visible", &loaded_project_settings.window_visible);
				draw_label("VSync");
				ImGui::Checkbox("##VSync", &loaded_project_settings.vsync_enabled);

				const char* aa_items[] = { "None", "FXAA" };
				int aa_index = static_cast<int>(loaded_project_settings.aa_mode);
				draw_label("Anti-Aliasing");
				if (ImGui::Combo("##Anti-Aliasing", &aa_index, aa_items, IM_ARRAYSIZE(aa_items)))
				{
					loaded_project_settings.aa_mode = static_cast<rendering::AntiAliasingMode>(aa_index);
					if (editor_viewport.view)
					{
						editor_viewport.view->options.aa_mode = loaded_project_settings.aa_mode;
					}
				}

				const char* tonemap_items[] = { "Reinhard", "ACES" };
				int tonemap_index = static_cast<int>(loaded_project_settings.tonemap_mode);
				draw_label("Tonemap Mode");
				if (ImGui::Combo("##Tonemap Mode", &tonemap_index, tonemap_items, IM_ARRAYSIZE(tonemap_items)))
				{
					loaded_project_settings.tonemap_mode = static_cast<rendering::TonemapMode>(tonemap_index);
					if (editor_viewport.view)
					{
						editor_viewport.view->options.tonemap_mode = loaded_project_settings.tonemap_mode;
					}
				}

				const char* backend_items[] = { "DirectX12", "Vulkan", "Metal" };
				int backend_index = 0;
				if (loaded_project_settings.backend_type == rendering::RHIBackend::Vulkan)
				{
					backend_index = 1;
				}
				else if (loaded_project_settings.backend_type == rendering::RHIBackend::Metal)
				{
					backend_index = 2;
				}
				draw_label("Backend");
				if (ImGui::Combo("##Backend", &backend_index, backend_items, IM_ARRAYSIZE(backend_items)))
				{
					loaded_project_settings.backend_type = backend_index == 1 ? rendering::RHIBackend::Vulkan : backend_index == 2 ? rendering::RHIBackend::Metal : rendering::RHIBackend::DirectX12;
				}

				ImGui::EndTable();
			}

			ImGui::Separator();
			if (ImGui::BeginTable("SplashSettingsTable", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				draw_label("Splash Enabled");
				ImGui::Checkbox("##Splash Enabled", &loaded_project_settings.splash_enabled);
				draw_label("Splash Title");
				draw_string_field("Splash Title", loaded_project_settings.splash_title);
				draw_label("Splash Status");
				draw_string_field("Splash Status", loaded_project_settings.splash_status);
				draw_label("Splash Image");
				draw_string_field("Splash Image", loaded_project_settings.splash_image);

				ImGui::EndTable();
			}

			ImGui::Separator();
			if (ImGui::BeginTable("GameDataSettingsTable", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);

				// Schema path row: input + New button
				draw_label("Game Data Schema");
				{
					const float btn_w = ImGui::CalcTextSize("New...").x + ImGui::GetStyle().FramePadding.x * 2.0f;
					ImGui::SetNextItemWidth(-(btn_w + ImGui::GetStyle().ItemSpacing.x));
					char buf[1024] = {};
					strncpy_s(buf, loaded_project_settings.game_data_schema.c_str(), sizeof(buf) - 1);
					if (ImGui::InputText("##gdschema", buf, sizeof(buf)))
					{
						loaded_project_settings.game_data_schema = buf;
						game_data_editor.loaded_schema_path.clear();
					}
					ImGui::SameLine();
					const bool no_project = loaded_project_settings.settings_path.empty();
					if (no_project)
					{
						ImGui::BeginDisabled();
					}
					if (ImGui::Button("New...##gdnew"))
					{
						ImGui::OpenPopup("##gd_new_schema_popup");
					}
					if (no_project)
					{
						ImGui::EndDisabled();
					}
					if (ImGui::BeginPopup("##gd_new_schema_popup"))
					{
						ImGui::TextUnformatted("File name:");
						ImGui::SetNextItemWidth(200.0f);
						ImGui::InputText("##gd_schema_fname", game_data_editor.new_schema_filename, sizeof(game_data_editor.new_schema_filename));
						if (ImGui::Button("Create##gdcreate"))
						{
							if (game_data_editor.new_schema_filename[0] != '\0')
							{
								String filename = game_data_editor.new_schema_filename;
								if (io::GetExtension(filename) != resource::game_data_schema_extension)
								{
									filename += ".";
									filename += resource::game_data_schema_extension;
								}
								const String config_dir = io::CombinePath(project::GetContentRoot(loaded_project_settings), "Config");
								io::CreateDirectories(config_dir);
								const String new_path = io::CombinePath(config_dir, filename);
								game::GameData new_gd;
								if (new_gd.SaveSchema(new_path.c_str()))
								{
									loaded_project_settings.game_data_schema = String("Config/") + filename;
									game_data_editor.game_data = std::move(new_gd);
									game_data_editor.loaded_schema_path = loaded_project_settings.game_data_schema;
									game_data_editor.dirty = false;
									game_data_editor.new_schema_filename[0] = '\0';
								}
								ImGui::CloseCurrentPopup();
							}
						}
						ImGui::SameLine();
						if (ImGui::Button("Cancel##gdcancel"))
						{
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}
				}

				ImGui::EndTable();
			}

			// sync schema when path changes
			const String& schema_setting = loaded_project_settings.game_data_schema;
			if (schema_setting != game_data_editor.loaded_schema_path)
			{
				game_data_editor.game_data = game::GameData{};
				if (!schema_setting.empty())
				{
					const String full_path = project::ResolveProjectContentPath(contents_root_dir, schema_setting);
					game_data_editor.game_data.LoadSchema(full_path.c_str());
				}
				game_data_editor.loaded_schema_path = schema_setting;
				game_data_editor.dirty = false;
			}

			if (!schema_setting.empty())
			{
				ImGui::Spacing();
				game::GameData& gd = game_data_editor.game_data;
				static const char* type_names[] = { "string", "int", "float", "bool" };

				constexpr ImGuiTableFlags table_flags =
					ImGuiTableFlags_Borders |
					ImGuiTableFlags_RowBg |
					ImGuiTableFlags_SizingStretchProp;

				if (ImGui::BeginTable("##gd_table", 4, table_flags))
				{
					ImGui::TableSetupColumn("Key",     ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthFixed, 70.0f);
					ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn("",        ImGuiTableColumnFlags_WidthFixed, 26.0f);
					ImGui::TableHeadersRow();

					int remove_index = -1;
					for (uint32 i = 0; i < gd.GetFieldCount(); ++i)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0);
						ImGui::TextUnformatted(gd.GetFieldKey(i));
						ImGui::TableSetColumnIndex(1);
						ImGui::TextUnformatted(gd.GetFieldType(i));
						ImGui::TableSetColumnIndex(2);
						ImGui::TextUnformatted(gd.GetFieldDefault(i));
						ImGui::TableSetColumnIndex(3);
						const String del_id = String("X##d") + std::to_string(i);
						if (ImGui::SmallButton(del_id.c_str()))
						{
							remove_index = static_cast<int>(i);
						}
					}

					ImGui::EndTable();

					if (remove_index >= 0)
					{
						const char* key = gd.GetFieldKey(static_cast<uint32>(remove_index));
						if (key)
						{
							gd.RemoveField(key);
							game_data_editor.dirty = true;
						}
					}
				}

				ImGui::Spacing();
				ImGui::SetNextItemWidth(150.0f);
				ImGui::InputText("##gd_newkey", game_data_editor.new_key, sizeof(game_data_editor.new_key));
				ImGui::SameLine();
				ImGui::SetNextItemWidth(80.0f);
				ImGui::Combo("##gd_newtype", &game_data_editor.new_type_index, type_names, IM_ARRAYSIZE(type_names));
				ImGui::SameLine();
				ImGui::SetNextItemWidth(120.0f);
				ImGui::InputText("##gd_newdefault", game_data_editor.new_default, sizeof(game_data_editor.new_default));
				ImGui::SameLine();
				if (ImGui::Button("Add##gd_add"))
				{
					if (game_data_editor.new_key[0] != '\0')
					{
						gd.AddField(
							game_data_editor.new_key,
							type_names[game_data_editor.new_type_index],
							game_data_editor.new_default);
						game_data_editor.new_key[0] = '\0';
						game_data_editor.new_default[0] = '\0';
						game_data_editor.dirty = true;
					}
				}

				ImGui::Spacing();
				const bool schema_save_disabled = !game_data_editor.dirty;
				if (schema_save_disabled)
				{
					ImGui::BeginDisabled();
				}
				if (ImGui::Button("Save Schema##gd_save"))
				{
					const String full_path = project::ResolveProjectContentPath(contents_root_dir, schema_setting);
					io::CreateDirectories(io::GetDirectoryFromPath(full_path));
					if (gd.SaveSchema(full_path.c_str()))
					{
						game_data_editor.dirty = false;
					}
				}
				if (schema_save_disabled)
				{
					ImGui::EndDisabled();
				}
			}

			ImGui::Separator();
			const bool can_save_project = !loaded_project_settings.settings_path.empty();
			if (!can_save_project)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button(editor_text::save_project))
			{
				SaveProject();
			}
			if (!can_save_project)
			{
				ImGui::EndDisabled();
			}
		}
		ImGui::End();
	}

	bool EditorApplication::SaveScene(const String& path)
	{
		if (path.empty() || !editor_viewport.view || !editor_viewport.view->scene)
		{
			return false;
		}

		Vector<ecs::Entity> excluded_entities;
		if (editor_viewport.view->camera_entity != ecs::INVALID_ENTITY)
		{
			excluded_entities.push_back(editor_viewport.view->camera_entity);
		}
		if (editor_viewport.debug_primitive_entity != ecs::INVALID_ENTITY)
		{
			excluded_entities.push_back(editor_viewport.debug_primitive_entity);
		}

		won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Write);
		won::serialize::SceneSerializeDesc desc = {};
		desc.excluded_entities = &excluded_entities;
		won::serialize::Serialize(archive, *editor_viewport.view->scene, desc);
		if (archive.HasError())
		{
			backlog::Post(editor_text::save_scene_failed + archive.GetError(), backlog::LogLevel::Warning);
			return false;
		}

		String directory = io::GetDirectoryFromPath(path);
		if (!directory.empty() && !io::CreateDirectories(directory))
		{
			backlog::Post(editor_text::save_scene_failed + directory, backlog::LogLevel::Warning);
			return false;
		}
		if (!archive.SaveToFile(path))
		{
			backlog::Post(editor_text::save_scene_failed + path, backlog::LogLevel::Warning);
			return false;
		}

		current_scene_path = path;
		String relative_path = io::GetRelativePath(contents_root_dir, path);
		String config_path = relative_path.empty() ? path : relative_path;
		editor_settings.last_scene_path = config_path;
		const String startup_scene_path = loaded_project_settings.startup_scene.empty()
			? String()
			: project::ResolveProjectContentPath(contents_root_dir, loaded_project_settings.startup_scene);
		if (loaded_project_settings.startup_scene.empty() || !io::IsFile(startup_scene_path))
		{
			loaded_project_settings.startup_scene = config_path;
			SaveProject();
		}
		RebuildContentBrowser();
		backlog::Post(editor_text::scene_saved + path);
		return true;
	}

	void EditorApplication::RebindSceneResources()
	{
		if (!editor_viewport.view || !editor_viewport.view->scene || !device)
		{
			return;
		}

		ecs::Scene& scene = *editor_viewport.view->scene;
		if (auto geometry_array = scene.GetComponentArray<ecs::GeometryComponent>())
		{
			for (Size i = 0; i < geometry_array->GetSize(); ++i)
			{
				ecs::GeometryComponent& geometry = geometry_array->data[i];
				if (geometry.mesh_asset_path.empty())
				{
					continue;
				}

				String disk_path = project::ResolveProjectContentPath(contents_root_dir, geometry.mesh_asset_path);
				String binary_path;
				resource::AssetMeta meta = {};
				if (won::utils::ToLower(io::GetExtension(disk_path)) == resource::mesh_binary_extension)
				{
					binary_path = disk_path;
				}
				else if (resource::LoadAssetMeta(resource::GetAssetMetaPath(disk_path), meta))
				{
					binary_path = project::ResolveProjectContentPath(contents_root_dir, meta.binary_path);
				}

				std::shared_ptr<resource::Mesh> mesh = resource::LoadMeshBinary(binary_path);
				if (mesh && mesh->IsValid() && rendering::utils::CreateRenderData(*device, *mesh))
				{
					geometry.SetMesh(mesh);
				}
			}
		}

		if (auto material_array = scene.GetComponentArray<ecs::MaterialComponent>())
		{
			for (Size i = 0; i < material_array->GetSize(); ++i)
			{
				ecs::MaterialComponent& material = material_array->data[i];
				if (!material.material && !material.material_asset_path.empty())
				{
					material.SetMaterial(resource::LoadMaterialBinary(project::ResolveProjectContentPath(contents_root_dir, material.material_asset_path)));
				}
				if (!material.material)
				{
					continue;
				}
				for (resource::MaterialSlot& material_slot : material.material->slots)
				{
					for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
					{
						resource::MaterialSlot::TextureMap& texture_map = material_slot.textures[texture_slot];
						if (texture_map.texture_asset_path.empty())
						{
							continue;
						}

						String disk_path = project::ResolveProjectContentPath(contents_root_dir, texture_map.texture_asset_path);
						std::shared_ptr<resource::Image> image;
						if (won::utils::ToLower(io::GetExtension(disk_path)) == resource::texture_binary_extension)
						{
							image = resource::LoadTextureBinary(disk_path);
						}
						else
						{
							resource::AssetMeta meta = {};
							if (resource::LoadAssetMeta(resource::GetAssetMetaPath(disk_path), meta))
							{
								String binary_path = project::ResolveProjectContentPath(contents_root_dir, meta.binary_path);
								image = resource::LoadTextureBinary(binary_path);
							}
							if (!image)
							{
								image = resource::LoadImageFile(disk_path, 4);
							}
						}

						const bool color_texture = texture_slot == BASECOLORMAP || texture_slot == EMISSIVEMAP || texture_slot == SHEENCOLORMAP;
						const RHIFormat texture_format = color_texture ? RHIFormat::R8G8B8A8UnormSrgb : RHIFormat::R8G8B8A8Unorm;
						if (image && image->IsValid() && rendering::utils::CreateRenderData(*device, *image, texture_format, true))
						{
							texture_map.texture = image->render_data.texture;
							texture_map.res_handle = image->render_data.srv;
						}
					}
				}
			}
		}

		if (auto text_array = scene.GetComponentArray<ecs::Text2DComponent>())
		{
			for (Size i = 0; i < text_array->GetSize(); ++i)
			{
				ecs::Text2DComponent& text = text_array->data[i];
				if (!text.font_asset_path.empty())
				{
					String font_path = project::ResolveProjectContentPath(contents_root_dir, text.font_asset_path);
					text.font = resource::LoadFontFile(font_path);
					text.SetDirty();
				}
			}
		}

		if (auto text_array = scene.GetComponentArray<ecs::Text3DComponent>())
		{
			for (Size i = 0; i < text_array->GetSize(); ++i)
			{
				ecs::Text3DComponent& text = text_array->data[i];
				if (!text.font_asset_path.empty())
				{
					String font_path = project::ResolveProjectContentPath(contents_root_dir, text.font_asset_path);
					text.font = resource::LoadFontFile(font_path);
					text.SetDirty();
				}
			}
		}

		if (auto script_array = scene.GetComponentArray<ecs::ScriptComponent>())
		{
			for (Size i = 0; i < script_array->GetSize(); ++i)
			{
				ecs::ScriptComponent& script_component = script_array->data[i];
				for (ecs::ScriptSlot& script_slot : script_component.scripts)
				{
					if (script_slot.script_path.rfind(project::content_virtual_root_prefix, 0) == 0)
					{
						script_slot.script_path = io::NormalizePath(script_slot.script_path.substr(project::content_virtual_root_prefix_length));
					}
					else if (io::IsAbsolutePath(script_slot.script_path))
					{
						const String relative_path = io::GetRelativePath(contents_root_dir, script_slot.script_path);
						script_slot.script_path = relative_path;
					}
					else
					{
						script_slot.script_path = io::NormalizePath(script_slot.script_path);
					}
					script_slot.initialized = false;
					script_slot.instance = {};
					script_slot.last_error.clear();
				}
			}
		}
	}

	void EditorApplication::LoadScene(const String& path)
	{
		if (path.empty() || !editor_viewport.view || !editor_viewport.view->scene)
		{
			return;
		}

		won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Read);
		if (!archive.LoadFromFile(path))
		{
			backlog::Post(editor_text::load_scene_failed + path, backlog::LogLevel::Warning);
			return;
		}

		won::serialize::Serialize(archive, *editor_viewport.view->scene);
		if (archive.HasError())
		{
			backlog::Post(editor_text::load_scene_warning + archive.GetError(), backlog::LogLevel::Warning);
		}

		current_scene_path = path;
		String relative_path = io::GetRelativePath(contents_root_dir, path);
		String config_path = relative_path.empty() ? path : relative_path;
		editor_settings.last_scene_path = config_path;
		editor_viewport.picked_entity = ecs::INVALID_ENTITY;
		editor_viewport.debug_primitive_entity = ecs::INVALID_ENTITY;
		editor_viewport.debug_primitive_mesh.reset();
		RebindSceneResources();
		CreateEditorCamera();
		UpdateEntityList();
		backlog::Post(editor_text::scene_loaded + path);
	}

	void EditorApplication::UpdateEntityList()
	{
		static std::mutex entity_list_mutex;
		std::lock_guard<std::mutex> lock(entity_list_mutex);

		auto& entities = editor_viewport.view->scene->GetEntities();

		sorted_entities.clear();
		sorted_entities.reserve(entities.size());
		for (ecs::Entity entity : entities)
		{
			if (entity == editor_viewport.view->camera_entity || entity == editor_viewport.debug_primitive_entity)
			{
				continue;
			}
			sorted_entities.push_back(entity);
		}

		std::sort(sorted_entities.begin(), sorted_entities.end());
	}

}
