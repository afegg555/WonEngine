#include "Editor.h"
#include "EditorTextKeys.h"
#include "FrameGraph.h"
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
#include "Console.h"
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
	static String contents_root_dir;
	static String editor_contents_root_dir;
	static String imgui_layout_path;
	static locale::LocaleDomain editor_locale;
	static UnorderedMap<String, String> editor_window_titles;
	static UnorderedMap<String, String> editor_field_labels;
	static UnorderedMap<String, String> editor_enum_labels;
	static uint32 editor_window_title_revision = 0;

	static const char* EditorText(const char* key)
	{
		return editor_locale.GetText(key).c_str();
	}

	static const char* EditorFieldText(const char* field_name)
	{
		if (!field_name)
		{
			return "";
		}
		auto it = editor_field_labels.find(field_name);
		if (it == editor_field_labels.end())
		{
			it = editor_field_labels.emplace(field_name, String("editor.field.") + field_name).first;
		}
		return editor_locale.GetText(it->second).c_str();
	}

	static const char* EditorEnumText(const char* type_display_name, const char* value_name)
	{
		String lookup = String("editor.enum.") + type_display_name + "." + value_name;
		auto it = editor_enum_labels.find(lookup);
		if (it == editor_enum_labels.end())
		{
			it = editor_enum_labels.emplace(std::move(lookup), String()).first;
			it->second = it->first;
		}
		return editor_locale.GetText(it->second).c_str();
	}

	template <typename EnumType>
	static bool DrawEnumCombo(const char* label, EnumType& value)
	{
		const won::TypeDesc* type_desc = reflection::TypeMeta<EnumType>::Get();
		Vector<const char*> items;
		items.reserve(type_desc->enum_value_count);
		int current_index = -1;
		for (uint32 i = 0; i < type_desc->enum_value_count; ++i)
		{
			const won::EnumValueDesc& enum_value = type_desc->enum_values[i];
			items.push_back(EditorEnumText(type_desc->display_name, enum_value.name));
			if (static_cast<int64>(value) == enum_value.value)
			{
				current_index = static_cast<int>(i);
			}
		}

		if (!ImGui::Combo(label, &current_index, items.data(), static_cast<int>(items.size())))
		{
			return false;
		}

		value = static_cast<EnumType>(type_desc->enum_values[current_index].value);
		return true;
	}

	static const char* EditorLabel(const char* key, const char* stable_id)
	{
		if (editor_window_title_revision != editor_locale.GetRevision())
		{
			editor_window_titles.clear();
			editor_window_title_revision = editor_locale.GetRevision();
		}
		auto it = editor_window_titles.find(key);
		if (it == editor_window_titles.end())
		{
			it = editor_window_titles.emplace(key, editor_locale.GetText(key) + stable_id).first;
		}
		return it->second.c_str();
	}
	namespace
	{
		template <typename Component>
		Component* FindComponent(ecs::Scene& scene, ecs::Entity entity)
		{
			auto component_array = scene.GetComponentArray<Component>();
			if (!component_array || !component_array->HasData(entity))
			{
				return nullptr;
			}
			return &component_array->GetData(entity);
		}


		namespace editor_asset_path
		{
			constexpr const char* generated_directory = "Generated";
			constexpr const char* scene_directory = "Scenes";
			constexpr const char* default_scene_file = "NewScene";
			constexpr const char* default_prefab_file = "NewPrefab";
		}

		namespace editor_window_id
		{
			constexpr const char* main = "###Main";
			constexpr const char* viewport = "###Viewport";
			constexpr const char* entity_list = "###EntityList";
			constexpr const char* inspector = "###Inspector";
			constexpr const char* log = "###Log";
			constexpr const char* profiler = "###Profiler";
			constexpr const char* contents_browser = "###ContentsBrowser";
			constexpr const char* project_settings = "###ProjectSettings";
			constexpr const char* project_localization = "###ProjectLocalization";
			constexpr const char* editor_preferences = "###EditorPreferences";
			constexpr const char* save_current_scene = "###SaveCurrentScenePopup";
		}

		namespace editor_popup_id
		{
			constexpr const char* options = "OptionsPopup";
			constexpr const char* add_component = "AddComponentPopup";
			constexpr const char* remove_component = "RemoveComponentPopup";
			constexpr const char* delete_entity_confirm = "DeleteEntityConfirmPopup";
			constexpr const char* import_content_asset = "ImportContentAssetPopup";
			constexpr const char* game_data_new_schema = "GameDataNewSchemaPopup";
		}

		namespace editor_shortcut
		{
			constexpr const char* undo = "Ctrl+Z";
			constexpr const char* redo = "Ctrl+Shift+Z";
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
			case reflection::TypeMeta<RectTransform2DComponent>::type_id:
			case reflection::TypeMeta<ButtonComponent>::type_id:
			case reflection::TypeMeta<LayoutComponent>::type_id:
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

		const won::UnorderedMap<won::TypeId, won::Vector<won::TypeId>>& GetComponentCompanionTable()
		{
			static const won::UnorderedMap<won::TypeId, won::Vector<won::TypeId>> table = {
				{ reflection::TypeMeta<RectTransform2DComponent>::type_id, { reflection::TypeMeta<HierarchyComponent>::type_id } },
				{ reflection::TypeMeta<Sprite2DComponent>::type_id, { reflection::TypeMeta<RectTransform2DComponent>::type_id, reflection::TypeMeta<MaterialComponent>::type_id } },
				{ reflection::TypeMeta<Text2DComponent>::type_id, { reflection::TypeMeta<RectTransform2DComponent>::type_id, reflection::TypeMeta<MaterialComponent>::type_id } },
				{ reflection::TypeMeta<ButtonComponent>::type_id, { reflection::TypeMeta<RectTransform2DComponent>::type_id } },
				{ reflection::TypeMeta<LayoutComponent>::type_id, { reflection::TypeMeta<RectTransform2DComponent>::type_id } },
				{ reflection::TypeMeta<Sprite3DComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id, reflection::TypeMeta<MaterialComponent>::type_id } },
				{ reflection::TypeMeta<Text3DComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id, reflection::TypeMeta<MaterialComponent>::type_id } },
				{ reflection::TypeMeta<GeometryComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id, reflection::TypeMeta<MaterialComponent>::type_id } },
				{ reflection::TypeMeta<Rigidbody3DComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id, reflection::TypeMeta<Collider3DComponent>::type_id } },
				{ reflection::TypeMeta<Collider3DComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id } },
				{ reflection::TypeMeta<LightComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id } },
				{ reflection::TypeMeta<CameraComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id } },
				{ reflection::TypeMeta<ReflectionProbeComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id } },
				{ reflection::TypeMeta<WaterBodyComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id } },
				{ reflection::TypeMeta<WaterZoneComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id } },
				{ reflection::TypeMeta<AudioSourceComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id } },
				{ reflection::TypeMeta<DecalComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id, reflection::TypeMeta<MaterialComponent>::type_id } },
				{ reflection::TypeMeta<ParticleEmitter3DComponent>::type_id, { reflection::TypeMeta<TransformComponent>::type_id, reflection::TypeMeta<MaterialComponent>::type_id } },
				{ reflection::TypeMeta<AnimationComponent>::type_id, { reflection::TypeMeta<GeometryComponent>::type_id } },
			};
			return table;
		}

		void* AddComponentWithCompanions(ecs::Scene* scene, ecs::Entity entity, const won::TypeDesc* type_desc)
		{
			if (!scene || !type_desc || scene->HasComponent(entity, type_desc->type_id))
			{
				return nullptr;
			}

			void* component = scene->AddComponent(entity, type_desc);

			const won::UnorderedMap<won::TypeId, won::Vector<won::TypeId>>& table = GetComponentCompanionTable();
			auto it = table.find(type_desc->type_id);
			if (it != table.end())
			{
				for (won::TypeId companion_id : it->second)
				{
					if (scene->HasComponent(entity, companion_id))
					{
						continue;
					}
					const won::TypeDesc* companion_desc = reflection::FindType(companion_id);
					if (companion_desc)
					{
						AddComponentWithCompanions(scene, entity, companion_desc); // recursively add companions
					}
				}
			}

			return component;
		}

		bool DrawComponentRemoveButton(const char* component_name, bool can_remove = true);

		bool DrawComponentCollapsingHeader(const char* label)
		{
			const float4& header = theme::component_header_color;
			const float4& header_hovered = theme::component_header_hovered_color;
			const float4& header_active = theme::component_header_active_color;
			ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(header.x, header.y, header.z, header.w));
			ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(header_hovered.x, header_hovered.y, header_hovered.z, header_hovered.w));
			ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(header_active.x, header_active.y, header_active.z, header_active.w));
			const bool open = ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_AllowOverlap);
			ImGui::PopStyleColor(3);
			return open;
		}

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
			const char* label = EditorFieldText(field.name);

			if (field.flag_values && field.flag_value_count > 0 && field.size == sizeof(uint32))
			{
				uint32* bits = static_cast<uint32*>(value);
				bool changed = false;
				ImGui::TextUnformatted(label);
				ImGui::Indent();
				for (uint32 i = 0; i < field.flag_value_count; ++i)
				{
					const won::EnumValueDesc& flag = field.flag_values[i];
					const uint32 mask = static_cast<uint32>(flag.value);
					if (mask == 0)
					{
						continue;
					}
					bool enabled = (*bits & mask) != 0;
					if (ImGui::Checkbox(EditorFieldText(flag.name), &enabled))
					{
						*bits = enabled ? (*bits | mask) : (*bits & ~mask);
						changed = true;
					}
				}
				ImGui::Unindent();
				return changed;
			}

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
				ImGui::TextDisabled(EditorText(editor_key::format_unsupported_format), label);
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
			const bool component_open = DrawComponentCollapsingHeader(component_name);
			const bool remove_component = DrawComponentRemoveButton(component_name);
			if (!remove_component && component_open && type_desc->fields && type_desc->field_count > 0)
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
				ImGui::SetTooltip(EditorText(editor_key::format_remove_tooltip_format), component_name);
			}

			if (pressed && can_remove)
			{
				ImGui::OpenPopup(editor_popup_id::remove_component);
			}

			bool remove_component = false;
			if (ImGui::BeginPopup(editor_popup_id::remove_component, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text(EditorText(editor_key::format_remove_confirm_format), component_name);
				ImGui::TextDisabled(EditorText(editor_key::label_action_cannot_be_undone));

				if (ImGui::Button(EditorText(editor_key::action_remove)))
				{
					remove_component = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button(EditorText(editor_key::action_cancel)))
				{
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			return remove_component;
		}

		String GetEditorFontFolder(const String& language)
		{
			const String normalized = locale::NormalizeLanguage(language);
			String folder = "Noto_Sans";
			if (normalized == "ko" || normalized.rfind("ko-", 0) == 0)
			{
				folder = "Noto_Sans_KR";
			}
			else if (normalized == "ja" || normalized.rfind("ja-", 0) == 0)
			{
				folder = "Noto_Sans_JP";
			}
			else if (normalized == "zh" || normalized.rfind("zh-", 0) == 0)
			{
				folder = "Noto_Sans_SC";
			}

			return editor_contents_root_dir + "Fonts/" + folder + "/static";
		}

		String GetEditorFontFileName(const String& language)
		{
			const String normalized = locale::NormalizeLanguage(language);
			if (normalized == "ko" || normalized.rfind("ko-", 0) == 0)
			{
				return "NotoSansKR-Regular.ttf";
			}
			if (normalized == "ja" || normalized.rfind("ja-", 0) == 0)
			{
				return "NotoSansJP-Regular.ttf";
			}
			if (normalized == "zh" || normalized.rfind("zh-", 0) == 0)
			{
				return "NotoSansSC-Regular.ttf";
			}
			return "NotoSans-Regular.ttf";
		}

		const ImWchar* GetEditorFontGlyphRanges(const String& language)
		{
			ImFontAtlas* font_atlas = ImGui::GetIO().Fonts;
			static const ImWchar latin_ranges[] =
			{
				0x0020, 0x00FF,
				0x0100, 0x024F,
				0x2000, 0x206F,
				0x20A0, 0x20CF,
				0,
			};
			static const ImWchar editor_symbol_ranges[] =
			{
				0x2212, 0x2212,
				0x25A0, 0x25FF,
				0,
			};
			const String normalized = locale::NormalizeLanguage(language);
			const ImWchar* language_ranges = latin_ranges;
			if (normalized == "ko" || normalized.rfind("ko-", 0) == 0)
			{
				language_ranges = font_atlas->GetGlyphRangesKorean();
			}
			else if (normalized == "ja" || normalized.rfind("ja-", 0) == 0)
			{
				language_ranges = font_atlas->GetGlyphRangesJapanese();
			}
			else if (normalized == "zh" || normalized.rfind("zh-", 0) == 0)
			{
				language_ranges = font_atlas->GetGlyphRangesChineseSimplifiedCommon();
			}

			static ImVector<ImWchar> merged_ranges;
			ImFontGlyphRangesBuilder builder;
			builder.AddRanges(language_ranges);
			if (language_ranges != latin_ranges)
			{
				builder.AddRanges(latin_ranges);
			}
			builder.AddRanges(editor_symbol_ranges);
			merged_ranges.clear();
			builder.BuildRanges(&merged_ranges);
			return merged_ranges.Data;
		}

		bool AddImGuiFont(const std::string& font_folder_path, const std::string& font_file_name, const ImWchar* glyph_ranges, bool merge_icon = true)
		{
			constexpr float font_size = 18.0f;
			ImGuiIO& io = ImGui::GetIO();
			io.Fonts->Clear();

			const std::string font_file_path = font_folder_path + "/" + font_file_name;
			ImFont* custom_font = io.Fonts->AddFontFromFileTTF(font_file_path.c_str(), font_size, nullptr, glyph_ranges);
			if (!custom_font)
			{
				custom_font = io.Fonts->AddFontDefault();
			}

			if (custom_font && merge_icon)
			{
				const std::string font_icon_path = editor_contents_root_dir + "Fonts/MaterialIcons-Regular.ttf";
				ImFontConfig config;
				config.MergeMode = true;
				config.GlyphOffset = ImVec2(0, 3);
				static const ImWchar icon_ranges[] = { ICON_MIN_MD, ICON_MAX_16_MD, 0 };
				io.Fonts->AddFontFromFileTTF(font_icon_path.c_str(), font_size, &config, icon_ranges);
			}

			return custom_font != nullptr;
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

			ImGui::DockBuilderDockWindow(editor_window_id::viewport, dock_main);
			ImGui::DockBuilderDockWindow(editor_window_id::inspector, dock_right);
			ImGui::DockBuilderDockWindow(editor_window_id::contents_browser, dock_bottom);
			ImGui::DockBuilderDockWindow(editor_window_id::log, dock_bottom);
			ImGui::DockBuilderDockWindow(editor_window_id::profiler, dock_bottom);
			ImGui::DockBuilderDockWindow(editor_window_id::entity_list, dock_left);

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

	}

	void EditorApplication::EditorViewport::CameraController::Update(const ecs::CameraComponent& camera, ecs::TransformComponent& transform, float dt, const float2& viewport_mouse_pos, const float2& viewport_size, bool can_begin_interaction)
	{
		XMVECTOR xforward = XMVector3Normalize(XMLoadFloat3(&camera.forward));
		XMVECTOR xup = XMVector3Normalize(XMLoadFloat3(&camera.up));
		XMVECTOR xright = XMVector3Normalize(XMVector3Cross(xup, xforward));

		const bool boost = io::IsDown(io::KEYBOARD_BUTTON_LSHIFT) || io::IsDown(io::KEYBOARD_BUTTON_RSHIFT);
		const float frame_move_speed = dt * move_speed * (boost ? move_boost_multiplier : 1.0f);

		if (can_begin_interaction && io::IsDown(io::Button('W')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xforward, frame_move_speed));
			transform.Translate(translation);
		}
		if (can_begin_interaction && io::IsDown(io::Button('A')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xright, -frame_move_speed));
			transform.Translate(translation);
		}
		if (can_begin_interaction && io::IsDown(io::Button('S')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xforward, -frame_move_speed));
			transform.Translate(translation);
		}
		if (can_begin_interaction && io::IsDown(io::Button('D')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xright, frame_move_speed));
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

		editor_settings.settings_path = io::CombinePath(io::GetExecutableDirectory(), editor_settings_file_name);
		LoadEditorSettings();
		InitializeEditorLanguage();

		loaded_project_settings = loaded_project_settings_in;
		const bool defer_main_window_show = splash && desc.project_settings.window_visible;
		if (splash)
		{
			splash->SetStatus(EditorText(editor_key::message_splash_starting_renderer));
		}
		ApplicationDesc initialize_desc = desc;
		initialize_desc.defer_window_show = defer_main_window_show;
		Application::Initialize(initialize_desc);
		ApplyProjectSettings(loaded_project_settings);
		if (splash)
		{
			splash->SetStatus(EditorText(editor_key::message_splash_loading_editor_settings));
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

		ecs::SceneDesc scene_desc = {};
		scene_desc.script_runtime = script_runtime.get();
		scene_desc.physics = project::GetPhysicsDesc(project_settings);
		scene_desc.audio_mixer = audio_mixer.get();
		scene_desc.enable_simulation = false;
		ecs::Scene& editor_scene = GetSceneManager()->CreateScene(scene_desc);
		edit_scene = &editor_scene;
		rendering::View editor_view = {};
		editor_view.scene = &editor_scene;
		editor_view.manual_camera = true;
		editor_view.options.resize_policy = rendering::ViewResizePolicy::Manual;
		editor_view.options.update_camera_aspect = false;
		editor_view.viewport.width = project_settings.window_width;
		editor_view.viewport.height = project_settings.window_height;
		editor_view.scissor.width = project_settings.window_width;
		editor_view.scissor.height = project_settings.window_height;
		uint32 editor_view_index = AddView(std::move(editor_view));
		editor_viewport.view = &GetView(editor_view_index);

		{
			ShaderCompilerOptions compiler_options;
			compiler_options.backend = ShaderCompilerBackend::DXC;
			compiler_options.shader_source_root_path = editor_contents_root_dir + "CustomShaders";
			std::unique_ptr<ShaderCompiler> compiler = CreateShaderCompiler(compiler_options);

			ShaderCompileDesc compile_desc;
			compile_desc.stage = RHIShaderStage::Vertex;
			compile_desc.source_file_name = "ImGuiVS.hlsl";
			ShaderCompileResult compile_result = compiler->Compile(compile_desc);

			imgui_vs = { RHIShaderStage::Vertex, compile_result.bytecode.data(), compile_result.bytecode.size()};
			
			compile_desc.stage = RHIShaderStage::Pixel;
			compile_desc.source_file_name = "ImGuiPS.hlsl";
			compile_result = compiler->Compile(compile_desc);

			imgui_ps = { RHIShaderStage::Pixel, compile_result.bytecode.data(), compile_result.bytecode.size() };
		}

		
		// Setup Dear ImGui context
		IMGUI_CHECKVERSION();
		ImGui::CreateContext();
		ImGuiIO& io = ImGui::GetIO();
		imgui_layout_path = io::CombinePath(io::GetExecutableDirectory(), editor_layout_file_name);
		io.IniFilename = imgui_layout_path.c_str();
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

		InitImGui();

		if (splash)
		{
			splash->SetStatus(EditorText(editor_key::message_splash_loading_plugins));
		}
		LoadPlugins();

		if (splash)
		{
			splash->SetStatus(EditorText(editor_key::message_splash_loading_startup_scene));
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
		if (splash)
		{
			splash->Close();
		}
		if (defer_main_window_show)
		{
			ShowMainWindow();
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
		imgui_font.reset();
		imgui_font_subresource = {};
		imgui_sampler.reset();
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
		if (editor_viewport.view)
		{
			editor_viewport.view->scene = nullptr;
			editor_viewport.view->camera_entity = ecs::INVALID_ENTITY;
		}
		editor_viewport.view = nullptr;
		plugins.clear();

		Application::Shutdown();
	}

	void EditorApplication::Update(float dt)
	{
		auto editor_update_range = profiler::ScopedRangeCPU("Update Editor");

		if (imgui_font_reload_pending)
		{
			imgui_font_reload_pending = false;
			RebuildImGuiFont(true);
		}

		if (request_play)
		{
			request_play = false;
			EnterPlay();
		}
		if (request_stop)
		{
			request_stop = false;
			ExitPlay();
		}

		io::SetMouseCaptured(is_playing && !editor_viewport.input_enabled);

		if (!is_playing && !ImGui::GetIO().WantTextInput)
		{
			const bool ctrl_down = io::IsDown(io::KEYBOARD_BUTTON_LCONTROL) || io::IsDown(io::KEYBOARD_BUTTON_RCONTROL);
			if (ctrl_down && io::IsPressed(static_cast<io::Button>(io::CHARACTER_RANGE_START + ('Z' - 'A'))))
			{
				const bool shift_down = io::IsDown(io::KEYBOARD_BUTTON_LSHIFT) || io::IsDown(io::KEYBOARD_BUTTON_RSHIFT);
				if (shift_down)
				{
					PerformRedo();
				}
				else
				{
					PerformUndo();
				}
			}
		}


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

			FinishBackgroundTask(task->bg_task_id, task->failed.load());

			if (!task->failed.load())
			{
				if (CommitAssetImportResult(*task))
				{
					entity_list_dirty = true;
					RebuildContentBrowser();
				}
				else
				{
					backlog::Post(EditorText(editor_key::message_content_browser_import_commit_failed) + task->path, backlog::LogLevel::Warning);
				}
			}
			else
			{
				backlog::Post(EditorText(editor_key::message_content_browser_import_failed) + task->path, backlog::LogLevel::Warning);
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

		if (editor_viewport.view)
		{
			editor_viewport.view->show_flags = editor_viewport.debug_settings.show_flags;
			editor_viewport.view->view_mode = editor_viewport.debug_settings.view_mode;
		}
		if (won::io::IsPressed(io::Button('R')))
		{
			renderer->ReloadShaders();
		}

		auto camera = editor_viewport.view->scene->GetComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity);
		auto transform = editor_viewport.view->scene->GetComponent<ecs::TransformComponent>(editor_viewport.view->camera_entity);
		if (!is_playing && camera && transform)
		{
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
		}

		simulation_paused = is_playing && is_paused && !request_step;
		request_step = false;
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
				RegisterPluginExtensions(editor_plugin_info.plugin, *edit_scene);
				editor_plugin_info.registered = true;
			}

			plugins.push_back(editor_plugin_info);
		}
	}

	void EditorApplication::RegisterPluginExtensions(const std::shared_ptr<plugin::Plugin>& plugin, ecs::Scene& scene)
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
					backlog::Post(EditorText(editor_key::message_failed_register_component_type) + extension.extension_id, backlog::LogLevel::Warning);
					continue;
				}

				scene.RegisterComponent(desc);
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

				scene.AddSystem(std::make_unique<PluginSystemAdapter>(plugin, desc));
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
				backlog::Post(EditorText(editor_key::message_failed_enable_plugin) + plugin_info.info.plugin_id, backlog::LogLevel::Warning);
			}
			else if (!plugin_info.registered)
			{
				RegisterPluginExtensions(plugin_info.plugin, *edit_scene);
				plugin_info.registered = true;
			}
		}
		else if (!enabled && plugin_info.plugin)
		{
			backlog::Post(EditorText(editor_key::message_plugin_disabled_next_restart) + plugin_info.info.plugin_id);
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

	uint64 EditorApplication::StartAssetImport(const String& path, bool add_to_scene)
	{
		const String tool_path = io::CombinePath(io::GetExecutableDirectory(), "AssetImporterTool.exe");
		if (path.empty() || !io::IsFile(tool_path))
		{
			backlog::Post(EditorText(editor_key::message_asset_importer_tool_not_found), backlog::LogLevel::Warning);
			return 0;
		}

		static uint64 import_task_counter = 0;
		auto task = std::make_shared<EditorAssetImporter::ImportTask>();
		task->path = path;
		task->id = ++import_task_counter;
		task->add_to_scene = add_to_scene;
		task->bg_task_id = AddBackgroundTask("Importing " + io::GetFilename(path));
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

	uint64 EditorApplication::AddBackgroundTask(const String& name)
	{
		BackgroundTask t;
		t.id = background_tasks.next_id++;
		t.name = name;
		t.state = BackgroundTask::State::Running;
		background_tasks.tasks.push_back(t);
		return t.id;
	}

	void EditorApplication::FinishBackgroundTask(uint64 id, bool failed)
	{
		for (BackgroundTask& t : background_tasks.tasks)
		{
			if (t.id == id)
			{
				t.state = failed ? BackgroundTask::State::Failed : BackgroundTask::State::Done;
				t.finished_time = ImGui::GetTime();
				return;
			}
		}
	}

	void EditorApplication::DrawBackgroundTaskStatus()
	{
		const double now = ImGui::GetTime();
		background_tasks.tasks.erase(std::remove_if(background_tasks.tasks.begin(), background_tasks.tasks.end(),
			[now](const BackgroundTask& t)
			{
				if (t.state == BackgroundTask::State::Done) return (now - t.finished_time) > 3.0;
				if (t.state == BackgroundTask::State::Failed) return (now - t.finished_time) > 10.0;
				return false;
			}), background_tasks.tasks.end());

		if (background_tasks.tasks.empty())
		{
			return;
		}

		const float pad = 12.0f;
		float anchor_x = 0.0f;
		float anchor_y = 0.0f;
		if (editor_viewport.view && editor_viewport.view->viewport.width > 0 && editor_viewport.view->viewport.height > 0)
		{
			const rendering::Rect& vp = editor_viewport.view->viewport;
			anchor_x = static_cast<float>(vp.x + vp.width) - pad;
			anchor_y = static_cast<float>(vp.y + vp.height) - pad;
		}
		else
		{
			const ImGuiViewport* main_vp = ImGui::GetMainViewport();
			anchor_x = main_vp->WorkPos.x + main_vp->WorkSize.x - pad;
			anchor_y = main_vp->WorkPos.y + main_vp->WorkSize.y - pad;
		}

		ImGui::SetNextWindowPos(ImVec2(anchor_x, anchor_y), ImGuiCond_Always, ImVec2(1.0f, 1.0f));
		ImGui::SetNextWindowBgAlpha(0.85f);
		const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_AlwaysAutoResize;
		if (ImGui::Begin("##background_tasks", nullptr, flags))
		{
			for (const BackgroundTask& t : background_tasks.tasks)
			{
				if (t.state == BackgroundTask::State::Running)
				{
					const float sz = ImGui::GetTextLineHeight();
					const ImVec2 cursor = ImGui::GetCursorScreenPos();
					const ImVec2 center = ImVec2(cursor.x + sz * 0.5f, cursor.y + sz * 0.5f);
					const float radius = sz * 0.35f;
					ImDrawList* draw_list = ImGui::GetWindowDrawList();
					const float start_angle = static_cast<float>(now) * 6.0f;
					const int segment_count = 16;
					draw_list->PathClear();
					for (int i = 0; i <= segment_count * 3 / 4; ++i)
					{
						const float a = start_angle + (static_cast<float>(i) / static_cast<float>(segment_count)) * 6.2831853f;
						draw_list->PathLineTo(ImVec2(center.x + cosf(a) * radius, center.y + sinf(a) * radius));
					}
					draw_list->PathStroke(ImGui::GetColorU32(ImGuiCol_Text), false, 2.0f);
					ImGui::Dummy(ImVec2(sz, sz));
					ImGui::SameLine();
					ImGui::TextUnformatted(t.name.c_str());
					if (t.progress >= 0.0f)
					{
						ImGui::SameLine();
						ImGui::ProgressBar(t.progress, ImVec2(120.0f, 0.0f));
					}
				}
				else if (t.state == BackgroundTask::State::Done)
				{
					ImGui::TextColored(ImVec4(0.40f, 0.85f, 0.40f, 1.0f), "done: %s", t.name.c_str());
				}
				else
				{
					ImGui::TextColored(ImVec4(0.90f, 0.35f, 0.35f, 1.0f), "failed: %s", t.name.c_str());
				}
			}
		}
		ImGui::End();
	}

	bool EditorApplication::CommitAssetImportResult(EditorAssetImporter::ImportTask& task)
	{
		if (!task.add_to_scene)
		{
			return true;
		}
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

		const String mat_binary_path = String(editor_asset_path::generated_directory) + "/" + meta.asset_id + "." + resource::material_binary_extension;
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
					texture_map.image = image;
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
	void EditorApplication::LoadEditorSettings()
	{
		EditorSettings loaded_settings = {};
		LoadSettings(editor_settings.settings_path, loaded_settings);
		loaded_settings.settings_path = editor_settings.settings_path;
		editor_settings = loaded_settings;

		content_browser.current_folder = editor_settings.content_current_folder;
		content_browser.type_filter = static_cast<ContentAssetType>(editor_settings.content_type_filter);
		content_browser.tile_size = (std::max)(48.0f, (std::min)(128.0f, editor_settings.content_tile_size));
		editor_viewport.debug_settings.show_flags = editor_settings.viewport_show_flags;
		editor_viewport.debug_settings.view_mode = static_cast<rendering::ViewMode>((std::max)(0, (std::min)(editor_settings.viewport_view_mode, static_cast<int>(rendering::ViewMode::VIEWMODE_COUNT) - 1)));
		editor_camera_speed = (std::max)(0.1f, editor_settings.camera_speed);
	}

	void EditorApplication::SaveEditorSettings()
	{
		editor_settings.content_current_folder = content_browser.current_folder;
		editor_settings.content_type_filter = static_cast<int>(content_browser.type_filter);
		editor_settings.content_tile_size = content_browser.tile_size;
		editor_settings.viewport_show_flags = editor_viewport.debug_settings.show_flags;
		editor_settings.viewport_view_mode = static_cast<int>(editor_viewport.debug_settings.view_mode);
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

	enum class AssetImportKind
	{
		None,
		Image,
		Mesh,
	};

	static AssetImportKind GetAssetImportKind(const String& extension)
	{
		const String ext = won::utils::ToLower(extension);
		if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "tga" || ext == "bmp") return AssetImportKind::Image;
		if (ext == "fbx" || ext == "obj" || ext == "gltf" || ext == "glb" || ext == "stl") return AssetImportKind::Mesh;
		return AssetImportKind::None;
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
			if (ext == resource::material_binary_extension) return ContentAssetType::Material;
			if (ext == "fbx" || ext == "obj" || ext == "gltf" || ext == "glb" || ext == "stl" || ext == resource::mesh_binary_extension) return ContentAssetType::Mesh;
			if (ext == resource::scene_file_extension) return ContentAssetType::Scene;
			if (ext == resource::prefab_file_extension) return ContentAssetType::Prefab;
			if (ext == "hlsl" || ext == "hlsli") return ContentAssetType::Shader;
			if (ext == "ttf" || ext == "otf") return ContentAssetType::Font;
			if (ext == "lua") return ContentAssetType::Script;
			if (ext == resource::sound_file_extension) return ContentAssetType::Sound;
			return ContentAssetType::Unknown;
		};

		Vector<io::DirectoryEntry> entries;
		if (!io::EnumerateDirectoryRecursive(contents_root_dir, &entries))
		{
			content_browser.current_folder = "/Contents";
			return;
		}

		auto resolve_content_path = [this](const String& path) -> String
		{
			if (path.empty())
			{
				return String();
			}
			return io::NormalizePath(io::IsAbsolutePath(path) ? path : io::CombinePath(contents_root_dir, path));
		};
		UnorderedMap<String, resource::AssetMeta> meta_by_binary; // binary_path -> meta

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
				resource::AssetMeta meta = {};
				if (resource::LoadAssetMeta(entry.path, meta) && !meta.binary_path.empty())
				{
					meta_by_binary[resolve_content_path(meta.binary_path)] = meta;
				}
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

		for (ContentBrowserAsset& asset : content_browser.assets)
		{
			const String extension = won::utils::ToLower(io::GetExtension(asset.disk_path));
			if (GetAssetImportKind(extension) != AssetImportKind::None)
			{
				resource::AssetMeta meta = {};
				if (resource::LoadAssetMeta(resource::GetAssetMetaPath(asset.disk_path), meta) && !meta.binary_path.empty())
				{
					if (!io::Exists(resolve_content_path(meta.binary_path)))
					{
						asset.has_broken_reference = true;
						asset.broken_reason = EditorText(editor_key::message_asset_binary_missing);
					}
					uint64 source_timestamp = 0;
					if (io::GetLastTimestamp(asset.disk_path, &source_timestamp) && source_timestamp > meta.source_timestamp)
					{
						asset.needs_reimport = true;
					}
				}
			}
			else if (extension == resource::mesh_binary_extension || extension == resource::texture_binary_extension || extension == resource::material_binary_extension)
			{
				auto found = meta_by_binary.find(io::NormalizePath(asset.disk_path));
				if (found != meta_by_binary.end())
				{
					asset.reimport_source_path = resolve_content_path(found->second.source_asset_path);
					if (!asset.reimport_source_path.empty() && !io::Exists(asset.reimport_source_path))
					{
						asset.has_broken_reference = true;
						asset.broken_reason = EditorText(editor_key::message_asset_source_missing);
					}
				}
				if (extension == resource::material_binary_extension)
				{
					if (std::shared_ptr<resource::Material> material = resource::LoadMaterialBinary(asset.disk_path))
					{
						for (const resource::MaterialSlot& slot : material->slots)
						{
							for (int texture_index = 0; texture_index < TEXTURESLOT_COUNT; ++texture_index)
							{
								const String& texture_path = slot.textures[texture_index].texture_asset_path;
								if (!texture_path.empty() && !io::Exists(resolve_content_path(texture_path)))
								{
									asset.has_broken_reference = true;
									asset.broken_reason = String(EditorText(editor_key::message_asset_texture_missing)) + io::GetFilename(texture_path);
								}
							}
						}
					}
				}
			}
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
			case ContentAssetType::Texture: return EditorText(editor_key::label_asset_texture);
			case ContentAssetType::Material: return EditorText(editor_key::label_asset_material);
			case ContentAssetType::Mesh: return EditorText(editor_key::label_asset_mesh);
			case ContentAssetType::Scene: return EditorText(editor_key::label_asset_scene);
			case ContentAssetType::Prefab: return EditorText(editor_key::label_asset_prefab);
			case ContentAssetType::Shader: return EditorText(editor_key::label_asset_shader);
			case ContentAssetType::Font: return EditorText(editor_key::label_asset_font);
			case ContentAssetType::Script: return EditorText(editor_key::label_asset_script);
			case ContentAssetType::Sound: return EditorText(editor_key::label_asset_sound);
			case ContentAssetType::Unknown: return EditorText(editor_key::label_unknown);
			default: return EditorText(editor_key::label_asset);
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
			case ContentAssetType::Prefab: return ICON_MD_WIDGETS;
			case ContentAssetType::Shader: return ICON_MD_CODE;
			case ContentAssetType::Font: return ICON_MD_FONT_DOWNLOAD;
			case ContentAssetType::Script: return ICON_MD_DESCRIPTION;
			case ContentAssetType::Sound: return ICON_MD_AUDIOTRACK;
			default: return ICON_MD_INSERT_DRIVE_FILE;
			}
		};

		auto to_u32 = [](const float4& color) { return ImGui::ColorConvertFloat4ToU32(ImVec4(color.x, color.y, color.z, color.w)); };
		auto to_vec4 = [](const float4& color) { return ImVec4(color.x, color.y, color.z, color.w); };

		ImGui::PushID(asset.virtual_path.c_str());
		ImGui::BeginGroup();
		const String asset_ext = won::utils::ToLower(io::GetExtension(asset.disk_path));
		const AssetImportKind import_kind = GetAssetImportKind(asset_ext);
		const bool is_imported_binary = asset_ext == resource::texture_binary_extension || asset_ext == resource::mesh_binary_extension || asset_ext == resource::material_binary_extension;
		const bool can_import_asset = import_kind != AssetImportKind::None;
		const bool can_import_to_scene = import_kind == AssetImportKind::Mesh;
		const bool can_load_scene = asset.type == ContentAssetType::Scene;
		const bool can_instantiate_prefab = asset.type == ContentAssetType::Prefab;
		ImGui::Button(type_icon(asset.type), ImVec2(tile_size, tile_size));
		if (import_kind != AssetImportKind::None || is_imported_binary)
		{
			const ImVec2 icon_min = ImGui::GetItemRectMin();
			const ImVec2 icon_max = ImGui::GetItemRectMax();
			const float dot_radius = 5.0f;
			const ImVec2 dot_center = ImVec2(icon_max.x - dot_radius - 3.0f, icon_min.y + dot_radius + 3.0f);
			const ImU32 dot_color = to_u32((import_kind != AssetImportKind::None) ? theme::asset_source_color : theme::asset_imported_color);
			ImGui::GetWindowDrawList()->AddCircleFilled(dot_center, dot_radius, dot_color);
		}
		if (asset.has_broken_reference || asset.needs_reimport)
		{
			const ImVec2 broken_icon_min = ImGui::GetItemRectMin();
			const float broken_dot_radius = 5.0f;
			const ImVec2 broken_dot_center = ImVec2(broken_icon_min.x + broken_dot_radius + 3.0f, broken_icon_min.y + broken_dot_radius + 3.0f);
			const ImU32 broken_dot_color = to_u32(asset.has_broken_reference ? theme::asset_broken_color : theme::asset_needs_reimport_color);
			ImGui::GetWindowDrawList()->AddCircleFilled(broken_dot_center, broken_dot_radius, broken_dot_color);
		}
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("CONTENT_ASSET_PATH", asset.disk_path.c_str(), asset.disk_path.size() + 1);
			ImGui::TextUnformatted(asset.name.c_str());
			ImGui::EndDragDropSource();
		}

		ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + tile_size);
		ImGui::TextWrapped("%s", asset.name.c_str());
		if (import_kind != AssetImportKind::None)
		{
			ImGui::TextColored(to_vec4(theme::asset_source_color), "%s (source)", type_name(asset.type));
		}
		else if (is_imported_binary)
		{
			ImGui::TextColored(to_vec4(theme::asset_imported_color), "%s (imported)", type_name(asset.type));
		}
		else
		{
			ImGui::TextDisabled("%s", type_name(asset.type));
		}
		ImGui::PopTextWrapPos();
		ImGui::EndGroup();

		if (can_import_asset && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			content_browser.pending_import_name = asset.name;
			content_browser.pending_import_virtual_path = asset.virtual_path;
			content_browser.pending_import_disk_path = asset.disk_path;
			content_browser.pending_import_type = asset.type;
			content_browser.pending_import_add_to_scene = can_import_to_scene;
			content_browser.open_import_confirm = true;
		}
		else if (can_load_scene && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			LoadScene(asset.disk_path);
		}
		else if (can_instantiate_prefab && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
		{
			InstantiatePrefab(asset.disk_path);
		}
		if (ImGui::BeginPopupContextItem("ContentAssetContext"))
		{
			if (ImGui::MenuItem(EditorText(editor_key::menu_import), nullptr, false, can_import_asset))
			{
				content_browser.pending_import_name = asset.name;
				content_browser.pending_import_virtual_path = asset.virtual_path;
				content_browser.pending_import_disk_path = asset.disk_path;
				content_browser.pending_import_type = asset.type;
				content_browser.pending_import_add_to_scene = false;
				content_browser.open_import_confirm = true;
			}
			if (ImGui::MenuItem(EditorText(editor_key::menu_import_to_scene), nullptr, false, can_import_to_scene))
			{
				content_browser.pending_import_name = asset.name;
				content_browser.pending_import_virtual_path = asset.virtual_path;
				content_browser.pending_import_disk_path = asset.disk_path;
				content_browser.pending_import_type = asset.type;
				content_browser.pending_import_add_to_scene = true;
				content_browser.open_import_confirm = true;
			}
			if (ImGui::MenuItem(EditorText(editor_key::menu_load_scene), nullptr, false, can_load_scene))
			{
				LoadScene(asset.disk_path);
			}
			if (ImGui::MenuItem(EditorText(editor_key::menu_add_to_scene), nullptr, false, can_instantiate_prefab))
			{
				InstantiatePrefab(asset.disk_path);
			}
			ImGui::Separator();
			if (ImGui::MenuItem(EditorText(editor_key::menu_copy_disk_path)))
			{
				ImGui::SetClipboardText(asset.disk_path.c_str());
			}
			if (ImGui::MenuItem(EditorText(editor_key::menu_copy_virtual_path)))
			{
				ImGui::SetClipboardText(asset.virtual_path.c_str());
			}
			ImGui::Separator();
			const bool can_reimport = !asset.reimport_source_path.empty() && io::Exists(asset.reimport_source_path);
			if (ImGui::MenuItem(EditorText(editor_key::menu_reimport), nullptr, false, can_reimport))
			{
				content_browser.pending_import_name = asset.name;
				content_browser.pending_import_virtual_path = asset.virtual_path;
				content_browser.pending_import_disk_path = asset.reimport_source_path;
				content_browser.pending_import_type = asset.type;
				content_browser.pending_import_add_to_scene = false;
				content_browser.open_import_confirm = true;
			}
			if (ImGui::MenuItem(EditorText(editor_key::menu_show_in_explorer)))
			{
				io::ShowInFileManager(asset.disk_path);
			}
			ImGui::EndPopup();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			ImGui::TextUnformatted(asset.virtual_path.c_str());
			ImGui::TextDisabled("%s", asset.disk_path.c_str());
			if (asset.has_broken_reference)
			{
				ImGui::TextColored(to_vec4(theme::asset_broken_color), "%s", asset.broken_reason.c_str());
			}
			else if (asset.needs_reimport)
			{
				ImGui::TextColored(to_vec4(theme::asset_needs_reimport_color), "%s", EditorText(editor_key::label_asset_needs_reimport));
			}
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
			case ContentAssetType::All: return EditorText(editor_key::label_all_types);
			case ContentAssetType::Texture: return EditorText(editor_key::label_asset_texture);
			case ContentAssetType::Material: return EditorText(editor_key::label_asset_material);
			case ContentAssetType::Mesh: return EditorText(editor_key::label_asset_mesh);
			case ContentAssetType::Scene: return EditorText(editor_key::label_asset_scene);
			case ContentAssetType::Prefab: return EditorText(editor_key::label_asset_prefab);
			case ContentAssetType::Shader: return EditorText(editor_key::label_asset_shader);
			case ContentAssetType::Font: return EditorText(editor_key::label_asset_font);
			case ContentAssetType::Script: return EditorText(editor_key::label_asset_script);
			case ContentAssetType::Sound: return EditorText(editor_key::label_asset_sound);
			case ContentAssetType::Unknown: return EditorText(editor_key::label_unknown);
			default: return EditorText(editor_key::label_all_types);
			}
		};

		ImGui::TextUnformatted(EditorText(editor_key::label_path_label));
		ImGui::SameLine();
		String current_folder = content_browser.current_folder;
		Vector<String> breadcrumb_parts;
		breadcrumb_parts.push_back(EditorText(editor_key::label_contents));
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
		ImGui::InputTextWithHint("##content_search", EditorText(editor_key::hint_search), content_browser.search, arraysize(content_browser.search));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::BeginCombo("##content_type_filter", type_name(content_browser.type_filter)))
		{
			const ContentAssetType filters[] = { ContentAssetType::All, ContentAssetType::Texture, ContentAssetType::Material, ContentAssetType::Mesh, ContentAssetType::Scene, ContentAssetType::Prefab, ContentAssetType::Shader, ContentAssetType::Font, ContentAssetType::Script, ContentAssetType::Sound, ContentAssetType::Unknown };
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
		ImGui::SliderFloat("##content_tile_size", &content_browser.tile_size, 48.0f, 128.0f, EditorText(editor_key::format_size_format));

		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_REFRESH))
		{
			RebuildContentBrowser();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip(EditorText(editor_key::label_refresh));
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
				ImGui::TextDisabled(EditorText(editor_key::label_folder));
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
			ImGui::TextDisabled(EditorText(editor_key::label_folder_empty));
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
			ImGui::OpenPopup(editor_popup_id::import_content_asset);
			content_browser.open_import_confirm = false;
		}

		if (ImGui::BeginPopup(editor_popup_id::import_content_asset, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted(EditorText(editor_key::label_import_content_asset_message));
			ImGui::TextUnformatted(content_browser.pending_import_name.c_str());
			ImGui::TextDisabled("%s", content_browser.pending_import_virtual_path.c_str());
			const bool can_import = !content_browser.pending_import_disk_path.empty() && GetAssetImportKind(io::GetExtension(content_browser.pending_import_disk_path)) != AssetImportKind::None;
			if (!can_import)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button(EditorText(editor_key::menu_import)))
			{
				StartAssetImport(content_browser.pending_import_disk_path, content_browser.pending_import_add_to_scene);
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
			if (ImGui::Button(EditorText(editor_key::action_cancel)))
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
		ImGui::Begin(editor_window_id::main, NULL, flags);

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
			if (ImGui::BeginMenu(EditorText(editor_key::menu_file)))
			{
				if (ImGui::MenuItem(EditorText(editor_key::menu_new_project), nullptr, false, !is_playing))
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
				if (ImGui::MenuItem(EditorText(editor_key::menu_load_project), nullptr, false, !is_playing))
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
				if (ImGui::MenuItem(EditorText(editor_key::menu_save_project), nullptr, false, !loaded_project_settings.settings_path.empty()))
				{
					SaveProject();
				}
					ImGui::TextDisabled("%s", loaded_project_settings.settings_path.empty() ? EditorText(editor_key::label_no_project) : loaded_project_settings.settings_path.c_str());
				ImGui::Separator();
				if (ImGui::MenuItem(EditorText(editor_key::menu_new_scene), nullptr, false, !is_playing))
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
				if (ImGui::MenuItem(EditorText(editor_key::menu_load_scene), nullptr, false, !is_playing))
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
				if (ImGui::MenuItem(EditorText(editor_key::menu_save_scene), nullptr, false, !is_playing))
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
				if (ImGui::MenuItem(EditorText(editor_key::menu_save_scene_as), nullptr, false, !is_playing))
				{
					save_as_then_load = false;
					save_as_then_new_scene = false;
					save_as_then_load_project = false;
					save_as_then_new_project = false;
					open_save_scene_as = true;
				}
				ImGui::TextDisabled("%s", current_scene_path.empty() ? EditorText(editor_key::label_unsaved_scene) : current_scene_path.c_str());
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(EditorText(editor_key::menu_edit)))
			{
				if (ImGui::MenuItem(EditorText(editor_key::window_project_settings)))
				{
					show_project_settings_window = true;
				}
				if (ImGui::MenuItem(EditorText(editor_key::menu_edit_project_localization)))
				{
					show_localization_window = true;
					ReloadLocalizationTables();
				}
				ImGui::Separator();
				String undo_label = editor_history.CanUndo() ? String(EditorText(editor_key::label_undo)) + " " + editor_history.PeekUndoName() : String(EditorText(editor_key::label_undo));
				if (ImGui::MenuItem(undo_label.c_str(), editor_shortcut::undo, false, !is_playing && editor_history.CanUndo()))
				{
					PerformUndo();
				}
				String redo_label = editor_history.CanRedo() ? String(EditorText(editor_key::label_redo)) + " " + editor_history.PeekRedoName() : String(EditorText(editor_key::label_redo));
				if (ImGui::MenuItem(redo_label.c_str(), editor_shortcut::redo, false, !is_playing && editor_history.CanRedo()))
				{
					PerformRedo();
				}
				ImGui::Separator();
				if (ImGui::MenuItem(EditorText(editor_key::menu_edit_preferences)))
				{
					show_editor_preferences_window = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(EditorText(editor_key::menu_build)))
			{
				const bool can_package_project = !loaded_project_settings.settings_path.empty();
				if (ImGui::MenuItem(EditorText(editor_key::menu_package_project), nullptr, false, !is_playing && can_package_project))
				{
					const bool scene_saved = current_scene_path.empty() || SaveScene(current_scene_path);
					if (scene_saved)
					{
						project::SaveSettings(loaded_project_settings.settings_path, loaded_project_settings);

						const String package_tool_path = io::CombinePath(io::GetExecutableDirectory(), "PackageTool.exe");
						if (!io::IsFile(package_tool_path))
						{
							backlog::Post(EditorText(editor_key::message_package_tool_not_found) + package_tool_path, backlog::LogLevel::Warning);
						}
						else
						{
							const String arguments = "\"" + loaded_project_settings.settings_path + "\" Release";
							const bool launched = io::LaunchProcess(package_tool_path, arguments, io::GetExecutableDirectory());
							backlog::Post(launched ? EditorText(editor_key::message_package_project_started) + loaded_project_settings.settings_path : EditorText(editor_key::message_package_project_failed), launched ? backlog::LogLevel::Default : backlog::LogLevel::Warning);
						}
					}
				}
				if (!can_package_project)
				{
					ImGui::TextDisabled(EditorText(editor_key::message_package_project_missing_settings));
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(EditorText(editor_key::menu_window)))
			{
				if (ImGui::MenuItem(EditorText(editor_key::menu_window_reset_layout)))
				{
					BuildDefaultDockLayout(dockspace_id, io.DisplaySize);
					focus_contents_browser_on_startup = true;
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu(EditorText(editor_key::menu_plugins)))
			{
				if (plugins.empty())
				{
					ImGui::TextDisabled(EditorText(editor_key::label_no_plugins_found));
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
						const char* type_name = EditorText(editor_key::label_unknown);
						if (plugin_info.info.type == plugin::PluginType::EditorDefault) { type_name = "EditorDefault"; }
						else if (plugin_info.info.type == plugin::PluginType::EditorOptional) { type_name = "EditorOptional"; }
						else if (plugin_info.info.type == plugin::PluginType::RuntimeOptional) { type_name = "RuntimeOptional"; }
						ImGui::SetTooltip("%s\n%s\n%s", plugin_info.info.plugin_id.c_str(), type_name, plugin_info.plugin ? EditorText(editor_key::message_plugin_loaded) : EditorText(editor_key::message_plugin_not_loaded));
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
			if (ImGui::Button(ICON_MD_MINIMIZE, ImVec2(button_size, button_size)))
			{
				window->Minimize();
			}
			ImGui::SameLine();
			if (ImGui::Button(window->IsMaximized() ? ICON_MD_FILTER_NONE : ICON_MD_CROP_SQUARE, ImVec2(button_size, button_size)))
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
			if (ImGui::Button(ICON_MD_CLOSE, ImVec2(button_size, button_size)))
			{
				window->Close();
			}
#endif

			ImGui::EndMenuBar();
		}

		if (open_save_current_scene)
		{
			ImGui::OpenPopup(editor_window_id::save_current_scene);
		}

		if (ImGui::BeginPopupModal(EditorLabel(editor_key::window_save_current_scene, editor_window_id::save_current_scene), nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted(EditorText(editor_key::label_save_current_scene_message));
			if (ImGui::Button(current_scene_path.empty() ? EditorText(editor_key::action_save_as) : EditorText(editor_key::action_save)))
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
			if (ImGui::Button(EditorText(editor_key::action_dont_save)))
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
			if (ImGui::Button(EditorText(editor_key::action_cancel)))
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
			desc.title = EditorText(editor_key::menu_save_scene_as);
			desc.initial_directory = io::CombinePath(contents_root_dir, editor_asset_path::scene_directory);
			desc.default_file_name = String(editor_asset_path::default_scene_file) + "." + resource::scene_file_extension;
			desc.default_extension = resource::scene_file_extension;
			desc.filter_name = EditorText(editor_key::label_won_scene_file);
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
				CreateStartupScene();
				UpdateEntityList();
			}
		}

		if (open_load_scene)
		{
			open_load_scene = false;

			io::FileDialogDesc desc = {};
			desc.owner_window = window ? window->GetNativeHandle() : nullptr;
			desc.title = EditorText(editor_key::menu_load_scene);
			desc.initial_directory = io::CombinePath(contents_root_dir, editor_asset_path::scene_directory);
			desc.default_extension = resource::scene_file_extension;
			desc.filter_name = EditorText(editor_key::label_won_scene_file);
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
			desc.title = EditorText(editor_key::menu_new_project);
			const String projects_directory = io::NormalizePath(String(PROJECTS_ROOT_DIR));
			desc.initial_directory = !loaded_project_settings.project_root.empty() ? loaded_project_settings.project_root : io::IsDirectory(projects_directory) ? projects_directory : io::GetExecutableDirectory();
			desc.default_file_name = String("NewProject.") + project::project_file_extension;
			desc.default_extension = project::project_file_extension;
			desc.filter_name = EditorText(editor_key::label_won_project_file);
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
			desc.title = EditorText(editor_key::menu_load_project);
			const String projects_directory = io::NormalizePath(String(PROJECTS_ROOT_DIR));
			desc.initial_directory = !loaded_project_settings.project_root.empty() ? loaded_project_settings.project_root : io::IsDirectory(projects_directory) ? projects_directory : io::GetExecutableDirectory();
			desc.default_extension = project::project_file_extension;
			desc.filter_name = EditorText(editor_key::label_won_project_file);
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
				desc.title = EditorText(editor_key::action_save_material_asset);
				desc.initial_directory = contents_root_dir;
				desc.default_file_name = String("Material.") + resource::material_binary_extension;
				desc.default_extension = resource::material_binary_extension;
				desc.filter_name = EditorText(editor_key::label_won_material_file);
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
						backlog::Post(EditorText(editor_key::message_save_material_failed) + path, backlog::LogLevel::Warning);
					}
				}
			}
		}

		ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

		ImGui::End();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		editor_viewport.input_enabled = false;
		if (ImGui::Begin(EditorLabel(editor_key::window_viewport, editor_window_id::viewport)))
		{
			String viewport_title = current_scene_path.empty() ? EditorText(editor_key::label_unsaved_scene) : io::GetFilename(current_scene_path);
			if (!current_scene_path.empty())
			{
				String extension = io::GetExtension(viewport_title);
				if (!extension.empty() && viewport_title.size() > extension.size() + 1)
				{
					viewport_title.resize(viewport_title.size() - extension.size() - 1);
				}
			}
			ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.52f, 0.52f, 0.49f, 1.0f));
			ImGui::TextUnformatted(ICON_MD_DATA_OBJECT);
			ImGui::SameLine(0.0f, 6.0f);
			ImGui::TextUnformatted(viewport_title.c_str());
			ImGui::PopStyleColor();

			{
				const ImGuiStyle& style = ImGui::GetStyle();
				auto button_width = [&style](const char* label) { return ImGui::CalcTextSize(label).x + style.FramePadding.x * 2.0f; };

				if (!is_playing)
				{
					const char* play_label = ICON_MD_PLAY_ARROW " Play";
					ImGui::SameLine();
					ImGui::SetCursorPosX((ImGui::GetWindowWidth() - button_width(play_label)) * 0.5f);
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.24f, 0.13f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.32f, 0.18f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.18f, 0.32f, 0.18f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.53f, 0.80f, 0.47f, 1.0f));
					if (ImGui::Button(play_label))
					{
						request_play = true;
					}
					ImGui::PopStyleColor(4);
				}
				else
				{
					const char* pause_label = is_paused ? ICON_MD_PLAY_ARROW " Resume" : ICON_MD_PAUSE " Pause";
					const char* step_label = ICON_MD_SKIP_NEXT " Step";
					const char* stop_label = ICON_MD_STOP " Stop";
					const float group_width = button_width(pause_label) + button_width(step_label) + button_width(stop_label) + style.ItemSpacing.x * 2.0f;
					ImGui::SameLine();
					ImGui::SetCursorPosX((ImGui::GetWindowWidth() - group_width) * 0.5f);

					if (ImGui::Button(pause_label))
					{
						is_paused = !is_paused;
					}
					ImGui::SameLine();
					ImGui::BeginDisabled(!is_paused);
					if (ImGui::Button(step_label))
					{
						request_step = true;
					}
					ImGui::EndDisabled();
					ImGui::SameLine();
					ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.36f, 0.16f, 0.16f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.46f, 0.20f, 0.20f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.46f, 0.20f, 0.20f, 1.0f));
					ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.90f, 0.58f, 0.58f, 1.0f));
					if (ImGui::Button(stop_label))
					{
						request_stop = true;
					}
					ImGui::PopStyleColor(4);
				}
			}

			{
				const float options_width = ImGui::CalcTextSize(ICON_MD_SETTINGS).x + ImGui::GetStyle().FramePadding.x * 2.0f;
				ImGui::SameLine();
				ImGui::SetCursorPosX(ImGui::GetWindowWidth() - options_width - ImGui::GetStyle().WindowPadding.x);
				if (ImGui::Button(ICON_MD_SETTINGS))
				{
					ImGui::OpenPopup(editor_popup_id::options);
				}
			}

			ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(500, 500));
			if (ImGui::BeginPopup(editor_popup_id::options))
			{
				ImGui::SetNextItemWidth(160.0f);
				DrawEnumCombo(EditorText(editor_key::label_view_mode), editor_viewport.debug_settings.view_mode);

				if (window)
				{
					RHISwapchain* swapchain = window->GetRHISwapchain();
					if (swapchain)
					{
						bool vsync_enabled = swapchain->IsVSyncEnabled();
						if (ImGui::Checkbox(EditorText(editor_key::label_vsync), &vsync_enabled))
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
					ImGui::SetNextItemWidth(120.0f);
					DrawEnumCombo(EditorText(editor_key::label_anti_aliasing), editor_viewport.view->options.aa_mode);

					ImGui::SetNextItemWidth(120.0f);
					DrawEnumCombo(EditorText(editor_key::label_tonemap_mode), editor_viewport.view->options.tonemap_mode);
				}

				ImGui::Separator();
				bool auto_exposure = editor_settings.editor_camera_auto_exposure;
				if (ImGui::Checkbox(EditorText(editor_key::label_auto_exposure), &auto_exposure))
				{
					editor_settings.editor_camera_auto_exposure = auto_exposure;
					ApplyEditorCameraExposure();
				}

				ImGui::BeginDisabled(auto_exposure);
				if (ImGui::DragFloat(EditorText(editor_key::label_fixed_ev100), &editor_settings.editor_camera_fixed_ev100, 0.1f, -10.0f, 20.0f))
				{
					ApplyEditorCameraExposure();
				}
				ImGui::EndDisabled();

				if (ImGui::DragFloat(EditorText(editor_key::label_exposure_compensation), &editor_settings.editor_camera_exposure_compensation, 0.01f, -16.0f, 16.0f))
				{
					ApplyEditorCameraExposure();
				}

				ImGui::BeginDisabled(!auto_exposure);
				bool auto_exposure_changed = false;
				auto_exposure_changed |= ImGui::DragFloat(EditorText(editor_key::label_auto_exposure_min_ev), &editor_settings.editor_camera_auto_exposure_min_ev, 0.1f, -16.0f, 32.0f);
				auto_exposure_changed |= ImGui::DragFloat(EditorText(editor_key::label_auto_exposure_max_ev), &editor_settings.editor_camera_auto_exposure_max_ev, 0.1f, -16.0f, 32.0f);
				auto_exposure_changed |= ImGui::DragFloat(EditorText(editor_key::label_auto_exposure_speed), &editor_settings.editor_camera_auto_exposure_speed, 0.05f, 0.0f, 100.0f);
				if (auto_exposure_changed)
				{
					ApplyEditorCameraExposure();
				}
				ImGui::EndDisabled();

				if (ImGui::Button(EditorText(editor_key::action_reset_editor_camera)))
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
				static const struct { uint32 flag; const char* text_key; } show_flag_items[] = {
					{ rendering::Show_Opaque,      editor_key::label_show_opaque },
					{ rendering::Show_Transparent, editor_key::label_show_transparent },
					{ rendering::Show_Decals,      editor_key::label_show_decals },
					{ rendering::Show_Water,       editor_key::label_show_water },
					{ rendering::Show_Particles,   editor_key::label_show_particles },
					{ rendering::Show_Sprites3D,   editor_key::label_show_sprites_3d },
					{ rendering::Show_Sprites2D,   editor_key::label_show_sprites_2d },
					{ rendering::Show_Shadows,     editor_key::label_show_shadows },
					{ rendering::Show_Grid,        editor_key::label_show_grid },
					{ rendering::Show_Colliders,   editor_key::label_show_colliders },
					{ rendering::Show_BVH,         editor_key::label_show_bvh },
					{ rendering::Show_DDGI,        editor_key::label_show_ddgi },
				};
				for (const auto& item : show_flag_items)
				{
					bool flag_enabled = (editor_viewport.debug_settings.show_flags & item.flag) != 0;
					if (ImGui::Checkbox(EditorText(item.text_key), &flag_enabled))
					{
						if (flag_enabled)
						{
							editor_viewport.debug_settings.show_flags |= item.flag;
						}
						else
						{
							editor_viewport.debug_settings.show_flags &= ~item.flag;
						}
					}
				}

				ImGui::Separator();
				if (ImGui::Button(EditorText(editor_key::action_close))) ImGui::CloseCurrentPopup();

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
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		DrawProjectSettingsWindow(&show_project_settings_window);
		DrawLocalizationWindow(&show_localization_window);
		DrawEditorPreferencesWindow(&show_editor_preferences_window);

		//ImGui::Begin("Scene Tree");
		//ImGui::Text("...");
		//ImGui::End();

		if (ImGui::Begin(EditorLabel(editor_key::window_entity_list, editor_window_id::entity_list)))
		{
			static int selected_index = -1;
			static ecs::Entity pending_delete_entity = INVALID_ENTITY;
			static ImVec2 delete_entity_popup_pos = {};
			ecs::Entity delete_entity = INVALID_ENTITY;
			bool open_delete_entity_confirm = false;
			ecs::Entity save_prefab_entity = INVALID_ENTITY;

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
				std::string import_status = running_import_count == 1 ? EditorText(editor_key::label_importing_asset) : EditorText(editor_key::label_importing_assets) + std::to_string(running_import_count) + ")";
				import_status.append(dot_count, '.');
				ImGui::TextDisabled("%s", import_status.c_str());
				ImGui::Separator();
			}

			if (ImGui::Button("+"))
			{
				ecs::Entity entity = editor_viewport.view->scene->CreateEntity();
				editor_history.PushEntityLifetime(*editor_viewport.view->scene, entity, String(), EditorText(editor_key::label_create_entity_command));
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

						std::string label = EditorText(editor_key::label_entity_label) + std::to_string(id);
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

							if (ImGui::MenuItem(EditorText(editor_key::menu_save_as_prefab)))
							{
								save_prefab_entity = id;
							}

							if (ImGui::MenuItem(EditorText(editor_key::menu_delete_entity), nullptr, false, id != editor_viewport.view->camera_entity))
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

			if (save_prefab_entity != INVALID_ENTITY)
			{
				io::FileDialogDesc desc = {};
				desc.owner_window = window ? window->GetNativeHandle() : nullptr;
				desc.title = EditorText(editor_key::menu_save_as_prefab);
				desc.initial_directory = contents_root_dir;
				desc.default_file_name = String(editor_asset_path::default_prefab_file) + "." + resource::prefab_file_extension;
				desc.default_extension = resource::prefab_file_extension;
				desc.filter_name = EditorText(editor_key::label_won_prefab_file);
				desc.filter_pattern = String("*.") + resource::prefab_file_extension;

				String path;
				if (io::SaveFileDialog(path, desc))
				{
					SavePrefab(path, save_prefab_entity);
				}
			}

			if (open_delete_entity_confirm)
			{
				ImGui::OpenPopup(editor_popup_id::delete_entity_confirm);
			}

			ImGui::SetNextWindowPos(delete_entity_popup_pos, ImGuiCond_Appearing);
			if (ImGui::BeginPopup(editor_popup_id::delete_entity_confirm, ImGuiWindowFlags_AlwaysAutoResize))
			{
				String entity_label = EditorText(editor_key::label_entity_label) + std::to_string(pending_delete_entity);
				if (ecs::NameComponent* name = editor_viewport.view->scene->GetComponent<ecs::NameComponent>(pending_delete_entity))
				{
					entity_label += " (" + name->value + ")";
				}

				ImGui::Text(EditorText(editor_key::format_delete_confirm_format), entity_label.c_str());
				ImGui::TextDisabled(EditorText(editor_key::label_delete_entity_children_warning));

				const bool can_delete_entity = pending_delete_entity != INVALID_ENTITY && pending_delete_entity != editor_viewport.view->camera_entity;
				if (!can_delete_entity)
				{
					ImGui::BeginDisabled();
				}
				if (ImGui::Button(EditorText(editor_key::action_delete_button)))
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
				if (ImGui::Button(EditorText(editor_key::action_cancel)))
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

					String undo_blob = EditorHistory::CaptureSubtree(*editor_viewport.view->scene, delete_entity);

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
									if (material_slot.textures[texture_slot].image && material_slot.textures[texture_slot].image->render_data.texture)
									{
										deferred_res_removal.resources.push_back(material_slot.textures[texture_slot].image->render_data.texture);
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
					editor_history.PushEntityLifetime(*editor_viewport.view->scene, delete_entity, std::move(undo_blob), EditorText(editor_key::label_delete_entity_command));
					ResetInspectorBaseline();
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

		if (ImGui::Begin(EditorLabel(editor_key::window_inspector, editor_window_id::inspector)))
		{
			if (editor_viewport.picked_entity != INVALID_ENTITY)
			{
				// TODO: use reflection system
				NameComponent* name_comp = editor_viewport.view->scene->GetComponent<NameComponent>(editor_viewport.picked_entity);
				if (name_comp)
				{
					ImGui::PushID("NameComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<NameComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<NameComponent>::display_name);

					if (!remove_component && component_open)
					{
						char name_buf[256];
						std::snprintf(name_buf, sizeof(name_buf), "%s", name_comp->value.c_str());

						if (ImGui::InputText(EditorText(editor_key::label_value), name_buf, sizeof(name_buf)))
						{
							name_comp->value = name_buf;
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<TransformComponent>::display_name);
					const bool can_remove_transform = editor_viewport.picked_entity != editor_viewport.view->camera_entity;
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<TransformComponent>::display_name, can_remove_transform);

					if (!remove_component && component_open)
					{
						float position[3] = { transform_comp->position.x, transform_comp->position.y, transform_comp->position.z };
						if (ImGui::DragFloat3(EditorText(editor_key::label_position), position, 0.01f))
						{
							transform_comp->position = { position[0], position[1], position[2] };
							transform_comp->SetDirty();
						}

						float3 rotation_xyz = QuaternionToEulerXYZDegrees(transform_comp->rotation);
						float rotation[3] = { rotation_xyz.x, rotation_xyz.y, rotation_xyz.z };
						if (ImGui::DragFloat3(EditorText(editor_key::label_rotation_xyz), rotation, 0.1f))
						{
							transform_comp->rotation = EulerXYZDegreesToQuaternion({ rotation[0], rotation[1], rotation[2] });
							transform_comp->SetDirty();
						}

						float scale[3] = { transform_comp->scale.x, transform_comp->scale.y, transform_comp->scale.z };
						if (ImGui::DragFloat3(EditorText(editor_key::label_scale), scale, 0.01f))
						{
							transform_comp->scale = { scale[0], scale[1], scale[2] };
							transform_comp->SetDirty();
						}
					}
					else if (remove_component && can_remove_transform)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<HierarchyComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<HierarchyComponent>::display_name);

					if (!remove_component && component_open)
					{
						uint64 parent_id = hierarchy_comp->parent_id;
						if (ImGui::InputScalar(EditorText(editor_key::label_parent), ImGuiDataType_U64, &parent_id))
						{
							hierarchy_comp->parent_id = parent_id;
						}
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<HierarchyComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				LightComponent* light_comp = FindComponent<LightComponent>(*editor_viewport.view->scene, editor_viewport.picked_entity);
				if (light_comp)
				{
					ImGui::PushID("LightComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<LightComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<LightComponent>::display_name);

					if (!remove_component && component_open)
					{
						// TODO: enum is hard coded
						bool light_changed = false;

						light_changed |= DrawEnumCombo(EditorText(editor_key::label_type), light_comp->type);

						float color[3] = { light_comp->color.x, light_comp->color.y, light_comp->color.z };
						if (ImGui::DragFloat3(EditorText(editor_key::label_color), color, 0.01f, 0.0f, 1.0f))
						{
							light_comp->color = { color[0], color[1], color[2] };
							light_changed = true;
						}

						light_changed |= ImGui::DragFloat(EditorText(editor_key::label_intensity), &light_comp->intensity, 1.0f, 0.0f, 100000.0f);
						light_changed |= ImGui::DragFloat(EditorText(editor_key::label_range), &light_comp->range, 0.1f, 0.0f, 100000.0f);
						light_changed |= ImGui::DragFloat(EditorText(editor_key::label_outer_cone), &light_comp->outer_cone_angle, 0.01f, 0.0f, math::PI);
						light_changed |= ImGui::DragFloat(EditorText(editor_key::label_inner_cone), &light_comp->inner_cone_angle, 0.01f, 0.0f, math::PI);

						float area_size[2] = { light_comp->area_size.x, light_comp->area_size.y };
						if (ImGui::DragFloat2(EditorText(editor_key::label_area_size), area_size, 0.1f, 0.01f, 1000.0f))
						{
							light_comp->area_size = { area_size[0], area_size[1] };
							light_changed = true;
						}

						bool is_two_sided = light_comp->IsTwoSided();
						if (ImGui::Checkbox(EditorText(editor_key::label_two_sided), &is_two_sided))
						{
							light_comp->SetTwoSided(is_two_sided);
							light_changed = true;
						}

						int shadow_map_resolution = static_cast<int>(light_comp->shadow_map_resolution);
						if (ImGui::InputInt(EditorText(editor_key::label_shadow_resolution), &shadow_map_resolution))
						{
							light_comp->shadow_map_resolution = (std::max)(1, shadow_map_resolution);
							light_changed = true;
						}

						int shadow_cascade_count = static_cast<int>(light_comp->shadow_cascade_count);
						if (ImGui::SliderInt(EditorText(editor_key::label_cascade_count), &shadow_cascade_count, 1, SHADOW_CASCADE_COUNT_MAX))
						{
							light_comp->shadow_cascade_count = static_cast<uint32>(shadow_cascade_count);
							light_changed = true;
						}

						light_changed |= ImGui::SliderFloat(EditorText(editor_key::label_cascade_lambda), &light_comp->shadow_cascade_lambda, 0.0f, 1.0f);
						light_changed |= ImGui::SliderFloat(EditorText(editor_key::label_cascade_blend), &light_comp->shadow_cascade_blend, 0.0f, 0.3f);

						if (ImGui::DragFloat(EditorText(editor_key::label_shadow_distance), &light_comp->shadow_distance, 1.0f, 0.0f, 100000.0f))
						{
							light_comp->shadow_distance = (std::max)(0.0f, light_comp->shadow_distance);
							light_changed = true;
						}

						bool is_active = light_comp->IsActive();
						if (ImGui::Checkbox(EditorText(editor_key::label_active), &is_active))
						{
							light_comp->SetActive(is_active);
							light_changed = true;
						}

						bool is_dynamic = light_comp->IsDynamic();
						if (ImGui::Checkbox(EditorText(editor_key::label_dynamic), &is_dynamic))
						{
							light_comp->SetDynamic(is_dynamic);
							light_changed = true;
						}

						bool is_cast_shadow = light_comp->IsCastShadow();
						if (ImGui::Checkbox(EditorText(editor_key::label_cast_shadow), &is_cast_shadow))
						{
							light_comp->SetCastShadow(is_cast_shadow);
							light_changed = true;
						}

						if (light_changed)
						{
							editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<LightComponent>());
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<CameraComponent>::display_name);
					const bool can_remove_camera = editor_viewport.picked_entity != editor_viewport.view->camera_entity;
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<CameraComponent>::display_name, can_remove_camera);

					if (!remove_component && component_open)
					{
						bool is_ortho = camera_comp->IsOrtho();
						if (ImGui::Checkbox(EditorText(editor_key::label_orthographic), &is_ortho))
						{
							camera_comp->SetOrtho(is_ortho);
						}

						float near_plane = camera_comp->near_plane;
						float far_plane = camera_comp->far_plane;
						bool near_far_changed = false;
						near_far_changed |= ImGui::DragFloat(EditorText(editor_key::label_near_plane), &near_plane, 0.01f, 0.001f, 100000.0f);
						near_far_changed |= ImGui::DragFloat(EditorText(editor_key::label_far_plane), &far_plane, 1.0f, 0.01f, 100000.0f);
						if (near_far_changed)
						{
							near_plane = (std::max)(0.001f, near_plane);
							far_plane = (std::max)(near_plane + 0.001f, far_plane);
							camera_comp->SetNearFar(near_plane, far_plane);
						}

						if (!camera_comp->IsOrtho())
						{
							float fov_y = camera_comp->fov_y;
							if (ImGui::DragFloat(EditorText(editor_key::label_fov_y), &fov_y, 0.01f, 0.01f, math::PI - 0.01f))
							{
								camera_comp->SetFOV_Y(fov_y);
							}
						}
						else
						{
							float ortho_vertical_size = camera_comp->ortho_vertical_size;
							if (ImGui::DragFloat(EditorText(editor_key::label_ortho_size), &ortho_vertical_size, 0.1f, 0.001f, 100000.0f))
							{
								camera_comp->SetOrthoVerticalSize(ortho_vertical_size);
							}
						}

						ImGui::Text(EditorText(editor_key::format_aspect_ratio_format), camera_comp->aspect_ratio);

						bool auto_exposure = camera_comp->IsAutoExposure();
						if (ImGui::Checkbox(EditorText(editor_key::label_auto_exposure), &auto_exposure))
						{
							camera_comp->SetAutoExposure(auto_exposure);
						}

						ImGui::Text(EditorText(editor_key::format_exposure_multiplier_format), camera_comp->exposure_multiplier);
						ImGui::DragFloat(EditorText(editor_key::label_exposure_compensation), &camera_comp->exposure_compensation, 0.01f, -16.0f, 16.0f);

						ImGui::BeginDisabled(!auto_exposure);
						ImGui::DragFloat(EditorText(editor_key::label_auto_exposure_min_ev), &camera_comp->auto_exposure_min_ev, 0.1f, -16.0f, 32.0f);
						ImGui::DragFloat(EditorText(editor_key::label_auto_exposure_max_ev), &camera_comp->auto_exposure_max_ev, 0.1f, -16.0f, 32.0f);
						ImGui::DragFloat(EditorText(editor_key::label_auto_exposure_speed), &camera_comp->auto_exposure_speed, 0.05f, 0.0f, 100.0f);
						ImGui::EndDisabled();

						ImGui::BeginDisabled(auto_exposure);
						ImGui::DragFloat(EditorText(editor_key::label_aperture), &camera_comp->aperture, 0.01f, 0.0f, 128.0f);
						ImGui::DragFloat(EditorText(editor_key::label_shutter_speed), &camera_comp->shutter_speed, 0.001f, 0.0001f, 100.0f);
						ImGui::DragFloat(EditorText(editor_key::label_sensitivity), &camera_comp->sensitivity, 1.0f, 1.0f, 102400.0f);
						ImGui::EndDisabled();
					}
					else if (remove_component && can_remove_camera)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<EnvironmentComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<EnvironmentComponent>::display_name);

					if (!remove_component && component_open)
					{
						bool is_active = environment_comp->IsActive();
						if (ImGui::Checkbox(EditorText(editor_key::label_active), &is_active))
						{
							environment_comp->SetActive(is_active);
						}

						DrawEnumCombo(EditorText(editor_key::label_sky_type), environment_comp->sky_type);

						const EnvironmentComponent::SkyType active_sky_type = environment_comp->sky_type;
						const bool sky_uses_sun = active_sky_type == EnvironmentComponent::SkyType::Procedural
							|| active_sky_type == EnvironmentComponent::SkyType::PhysicallyBased;
						const bool sky_uses_gradient = active_sky_type == EnvironmentComponent::SkyType::Procedural;
						const bool sky_uses_atmosphere = active_sky_type == EnvironmentComponent::SkyType::PhysicallyBased;
						const bool sky_uses_cubemap = active_sky_type == EnvironmentComponent::SkyType::Cubemap;
						const bool sky_uses_intensity = active_sky_type != EnvironmentComponent::SkyType::None;

						if (sky_uses_sun)
						{
							float sun_direction[3] = { environment_comp->sun_direction.x, environment_comp->sun_direction.y, environment_comp->sun_direction.z };
							if (ImGui::DragFloat3(EditorText(editor_key::label_sun_direction), sun_direction, 0.01f, -1.0f, 1.0f))
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
							if (ImGui::InputFloat3(EditorText(editor_key::label_sun_color), sun_color))
							{
								environment_comp->sun_color = { sun_color[0], sun_color[1], sun_color[2] };
							}

							ImGui::DragFloat(EditorText(editor_key::label_sun_intensity), &environment_comp->sun_intensity, 0.1f, 0.0f, 100000.0f);
							ImGui::DragFloat(EditorText(editor_key::label_sun_angular_radius), &environment_comp->sun_angular_radius, 0.0001f, 0.0f, 1.0f);
						}

						if (sky_uses_gradient)
						{
							ImGui::DragFloat(EditorText(editor_key::label_sun_glow_intensity), &environment_comp->sun_glow_intensity, 0.01f, 0.0f, 1000.0f);
							ImGui::DragFloat(EditorText(editor_key::label_sun_glow_falloff), &environment_comp->sun_glow_falloff, 0.1f, 0.0f, 1000.0f);

							float sky_horizon_color[3] = { environment_comp->sky_horizon_color.x, environment_comp->sky_horizon_color.y, environment_comp->sky_horizon_color.z };
							if (ImGui::InputFloat3(EditorText(editor_key::label_sky_horizon_color), sky_horizon_color))
							{
								environment_comp->sky_horizon_color = { sky_horizon_color[0], sky_horizon_color[1], sky_horizon_color[2] };
							}

							float sky_zenith_color[3] = { environment_comp->sky_zenith_color.x, environment_comp->sky_zenith_color.y, environment_comp->sky_zenith_color.z };
							if (ImGui::InputFloat3(EditorText(editor_key::label_sky_zenith_color), sky_zenith_color))
							{
								environment_comp->sky_zenith_color = { sky_zenith_color[0], sky_zenith_color[1], sky_zenith_color[2] };
							}
						}

						if (sky_uses_atmosphere)
						{
							ImGui::DragFloat(EditorText(editor_key::label_turbidity), &environment_comp->turbidity, 0.05f, 1.0f, 20.0f);
							ImGui::DragFloat(EditorText(editor_key::label_mie_eccentricity), &environment_comp->mie_eccentricity, 0.01f, -0.99f, 0.99f);
							ImGui::DragFloat(EditorText(editor_key::label_rayleigh_coefficient), &environment_comp->rayleigh_coefficient, 0.01f, 0.0f, 10.0f);
							ImGui::DragFloat(EditorText(editor_key::label_mie_coefficient), &environment_comp->mie_coefficient, 0.001f, 0.0f, 10.0f);
							ImGui::Checkbox(EditorText(editor_key::label_direct_sun_active), &environment_comp->direct_sun_active);
							ImGui::Checkbox(EditorText(editor_key::label_direct_sun_cast_shadow), &environment_comp->direct_sun_cast_shadow);
							int direct_sun_shadow_resolution = static_cast<int>(environment_comp->direct_sun_shadow_resolution);
							if (ImGui::InputInt(EditorText(editor_key::label_direct_sun_shadow_resolution), &direct_sun_shadow_resolution))
							{
								environment_comp->direct_sun_shadow_resolution = (std::max)(1, direct_sun_shadow_resolution);
							}
							int direct_sun_cascade_count = static_cast<int>(environment_comp->direct_sun_cascade_count);
							if (ImGui::SliderInt(EditorText(editor_key::label_direct_sun_cascade_count), &direct_sun_cascade_count, 1, SHADOW_CASCADE_COUNT_MAX))
							{
								environment_comp->direct_sun_cascade_count = static_cast<uint32>(direct_sun_cascade_count);
							}
							ImGui::SliderFloat(EditorText(editor_key::label_direct_sun_cascade_lambda), &environment_comp->direct_sun_cascade_lambda, 0.0f, 1.0f);
							ImGui::SliderFloat(EditorText(editor_key::label_direct_sun_cascade_blend), &environment_comp->direct_sun_cascade_blend, 0.0f, 0.3f);
							if (ImGui::DragFloat(EditorText(editor_key::label_direct_sun_shadow_distance), &environment_comp->direct_sun_shadow_distance, 1.0f, 0.0f, 100000.0f))
							{
								environment_comp->direct_sun_shadow_distance = (std::max)(0.0f, environment_comp->direct_sun_shadow_distance);
							}
						}

						if (sky_uses_cubemap)
						{
							char sky_cubemap_path[256] = {};
							strncpy(sky_cubemap_path, environment_comp->sky_cubemap_asset_path.c_str(), sizeof(sky_cubemap_path) - 1);
							if (ImGui::InputText(EditorText(editor_key::label_sky_cubemap_asset_path), sky_cubemap_path, sizeof(sky_cubemap_path)))
							{
								environment_comp->sky_cubemap_asset_path = sky_cubemap_path;
							}
						}

						if (sky_uses_intensity)
						{
							ImGui::DragFloat(EditorText(editor_key::label_sky_intensity), &environment_comp->sky_intensity, 1.0f, 0.0f, 1000000.0f);
						}

						if (sky_uses_gradient)
						{
							ImGui::DragFloat(EditorText(editor_key::label_sky_horizon_falloff), &environment_comp->sky_horizon_falloff, 0.01f, 0.0f, 1000.0f);

							float ground_horizon_color[3] = { environment_comp->ground_horizon_color.x, environment_comp->ground_horizon_color.y, environment_comp->ground_horizon_color.z };
							if (ImGui::InputFloat3(EditorText(editor_key::label_ground_horizon_color), ground_horizon_color))
							{
								environment_comp->ground_horizon_color = { ground_horizon_color[0], ground_horizon_color[1], ground_horizon_color[2] };
							}
						}

						if (sky_uses_sun)
						{
							float ground_color[3] = { environment_comp->ground_color.x, environment_comp->ground_color.y, environment_comp->ground_color.z };
							if (ImGui::InputFloat3(EditorText(editor_key::label_ground_color), ground_color))
							{
								environment_comp->ground_color = { ground_color[0], ground_color[1], ground_color[2] };
							}

							ImGui::DragFloat(EditorText(editor_key::label_ground_intensity), &environment_comp->ground_intensity, 1.0f, 0.0f, 1000000.0f);
						}

						if (sky_uses_gradient)
						{
							ImGui::DragFloat(EditorText(editor_key::label_ground_falloff), &environment_comp->ground_falloff, 0.01f, 0.0f, 1000.0f);
						}

						if (sky_uses_sun)
						{
							ImGui::SliderFloat(EditorText(editor_key::label_cloud_coverage), &environment_comp->cloud_coverage, 0.0f, 1.0f);
							ImGui::SliderFloat(EditorText(editor_key::label_cloud_density), &environment_comp->cloud_density, 0.0f, 1.0f);
							ImGui::DragFloat(EditorText(editor_key::label_cloud_frequency), &environment_comp->cloud_frequency, 0.01f, 0.01f, 100.0f);
							ImGui::DragFloat(EditorText(editor_key::label_cloud_speed), &environment_comp->cloud_speed, 0.001f, 0.0f, 10.0f);

							float cloud_color[3] = { environment_comp->cloud_color.x, environment_comp->cloud_color.y, environment_comp->cloud_color.z };
							if (ImGui::InputFloat3(EditorText(editor_key::label_cloud_color), cloud_color))
							{
								environment_comp->cloud_color = { cloud_color[0], cloud_color[1], cloud_color[2] };
							}

							float cloud_direction[2] = { environment_comp->cloud_direction.x, environment_comp->cloud_direction.y };
							if (ImGui::InputFloat2(EditorText(editor_key::label_cloud_direction), cloud_direction))
							{
								environment_comp->cloud_direction = { cloud_direction[0], cloud_direction[1] };
							}
						}

						DrawEnumCombo(EditorText(editor_key::label_gi_mode), environment_comp->diffuse_gi_mode);

						if (environment_comp->diffuse_gi_mode == EnvironmentComponent::DiffuseGIMode::Ambient)
						{
							float ambient_color[3] = { environment_comp->ambient_color.x, environment_comp->ambient_color.y, environment_comp->ambient_color.z };
							if (ImGui::InputFloat3(EditorText(editor_key::label_ambient_color), ambient_color))
							{
								environment_comp->ambient_color = { ambient_color[0], ambient_color[1], ambient_color[2] };
							}

							ImGui::DragFloat(EditorText(editor_key::label_ambient_intensity), &environment_comp->ambient_intensity, 1.0f, 0.0f, 1000000.0f);
						}

						if (environment_comp->diffuse_gi_mode == EnvironmentComponent::DiffuseGIMode::Cubemap)
						{
							char irradiance_path[256] = {};
							strncpy(irradiance_path, environment_comp->irradiance_cubemap_asset_path.c_str(), sizeof(irradiance_path) - 1);
							if (ImGui::InputText(EditorText(editor_key::label_irradiance_cubemap_asset_path), irradiance_path, sizeof(irradiance_path)))
							{
								environment_comp->irradiance_cubemap_asset_path = irradiance_path;
							}
						}

						DrawEnumCombo(EditorText(editor_key::label_reflection_mode), environment_comp->reflection_mode);

						if (environment_comp->reflection_mode == EnvironmentComponent::ReflectionMode::Cubemap)
						{
							char specular_path[256] = {};
							strncpy(specular_path, environment_comp->specular_cubemap_asset_path.c_str(), sizeof(specular_path) - 1);
							if (ImGui::InputText(EditorText(editor_key::label_specular_cubemap_asset_path), specular_path, sizeof(specular_path)))
							{
								environment_comp->specular_cubemap_asset_path = specular_path;
							}
						}

						ImGui::DragFloat(EditorText(editor_key::label_indirect_diffuse_scale), &environment_comp->indirect_diffuse_scale, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat(EditorText(editor_key::label_indirect_specular_scale), &environment_comp->indirect_specular_scale, 0.01f, 0.0f, 1000.0f);
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<FogVolumeComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<FogVolumeComponent>::display_name);

					if (!remove_component && component_open)
					{

					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<DDGIVolumeComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<DDGIVolumeComponent>::display_name);

					if (!remove_component && component_open)
					{
						bool is_active = ddgi_volume_comp->IsActive();
						if (ImGui::Checkbox(EditorText(editor_key::label_active), &is_active))
						{
							ddgi_volume_comp->SetActive(is_active);
						}

						bool is_dynamic = ddgi_volume_comp->IsDynamic();
						if (ImGui::Checkbox(EditorText(editor_key::label_dynamic), &is_dynamic))
						{
							ddgi_volume_comp->SetDynamic(is_dynamic);
						}

						int probe_counts[3] = {
							static_cast<int>(ddgi_volume_comp->probe_counts.x),
							static_cast<int>(ddgi_volume_comp->probe_counts.y),
							static_cast<int>(ddgi_volume_comp->probe_counts.z)
						};
						if (ImGui::InputInt3(EditorText(editor_key::label_probe_counts), probe_counts))
						{
							ddgi_volume_comp->probe_counts = {
								static_cast<uint32>((std::max)(1, probe_counts[0])),
								static_cast<uint32>((std::max)(1, probe_counts[1])),
								static_cast<uint32>((std::max)(1, probe_counts[2]))
							};
						}

						float probe_spacing[3] = { ddgi_volume_comp->probe_spacing.x, ddgi_volume_comp->probe_spacing.y, ddgi_volume_comp->probe_spacing.z };
						if (ImGui::InputFloat3(EditorText(editor_key::label_probe_spacing), probe_spacing))
						{
							ddgi_volume_comp->probe_spacing = { probe_spacing[0], probe_spacing[1], probe_spacing[2] };
						}

						float volume_offset[3] = { ddgi_volume_comp->volume_offset.x, ddgi_volume_comp->volume_offset.y, ddgi_volume_comp->volume_offset.z };
						if (ImGui::InputFloat3(EditorText(editor_key::label_volume_offset), volume_offset))
						{
							ddgi_volume_comp->volume_offset = { volume_offset[0], volume_offset[1], volume_offset[2] };
						}

						int probes_per_frame = static_cast<int>(ddgi_volume_comp->probes_per_frame);
						if (ImGui::InputInt(EditorText(editor_key::label_probes_per_frame), &probes_per_frame))
						{
							ddgi_volume_comp->probes_per_frame = static_cast<uint32>((std::max)(1, probes_per_frame));
						}

						int priority = static_cast<int>(ddgi_volume_comp->priority);
						if (ImGui::InputInt(EditorText(editor_key::label_priority), &priority))
						{
							ddgi_volume_comp->priority = static_cast<uint32>((std::max)(0, priority));
						}

						ImGui::SliderFloat(EditorText(editor_key::label_hysteresis), &ddgi_volume_comp->hysteresis, 0.0f, 1.0f);
						ImGui::DragFloat(EditorText(editor_key::label_normal_bias), &ddgi_volume_comp->normal_bias, 0.001f, 0.0f, 100.0f);
						ImGui::DragFloat(EditorText(editor_key::label_view_bias), &ddgi_volume_comp->view_bias, 0.001f, 0.0f, 100.0f);
						ImGui::DragFloat(EditorText(editor_key::label_max_distance), &ddgi_volume_comp->max_distance, 0.01f, 0.0f, 100000.0f);
						if (ImGui::Button(EditorText(editor_key::action_update_scene_gpubvh)))
						{
							editor_viewport.view->scene->BuildGPUBVH();
						}
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<DDGIVolumeComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				GeometryComponent* geometry_comp = FindComponent<GeometryComponent>(*editor_viewport.view->scene, editor_viewport.picked_entity);
				if (geometry_comp)
				{
					ImGui::PushID("GeometryComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<GeometryComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<GeometryComponent>::display_name);

					if (!remove_component && component_open)
					{
						String mesh_label = geometry_comp->mesh_asset_path.empty() ? String(EditorText(editor_key::label_none_placeholder)) : geometry_comp->mesh_asset_path;
						ImGui::TextUnformatted(EditorText(editor_key::label_mesh_asset));
						ImGui::SetNextItemWidth(-1.0f);
						if (ImGui::BeginCombo("##mesh_asset", mesh_label.c_str()))
						{
							for (const ContentBrowserAsset& asset : content_browser.assets)
							{
								if (asset.type != ContentAssetType::Mesh)
								{
									continue;
								}
								if (won::utils::ToLower(io::GetExtension(asset.disk_path)) != resource::mesh_binary_extension)
								{
									continue;
								}
								const String mesh_rel = io::GetRelativePath(contents_root_dir, asset.disk_path);
								if (mesh_rel.empty())
								{
									continue;
								}
								const bool mesh_selected = mesh_rel == geometry_comp->mesh_asset_path;
								if (ImGui::Selectable(asset.virtual_path.c_str(), mesh_selected))
								{
									auto loaded_mesh = resource::LoadMeshBinary(asset.disk_path);
									if (loaded_mesh && loaded_mesh->IsValid())
									{
										const ecs::Entity mesh_entity = editor_viewport.picked_entity;
										eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, mesh_entity, loaded_mesh, mesh_rel](const won::function::Value&) {
											GeometryComponent* geom = editor_viewport.view->scene->GetComponent<GeometryComponent>(mesh_entity);
											if (!geom || !device || !rendering::utils::CreateRenderData(*device, *loaded_mesh))
											{
												return;
											}
											if (geom->mesh && geom->mesh != loaded_mesh)
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
											geom->mesh_asset_path = mesh_rel;
											geom->SetMesh(loaded_mesh);
											editor_viewport.view->scene->SetBVHDirty();
										});
									}
								}
								if (mesh_selected)
								{
									ImGui::SetItemDefaultFocus();
								}
							}
							ImGui::EndCombo();
						}

						bool cast_shadow = geometry_comp->IsCastShadow();
						if (ImGui::Checkbox(EditorText(editor_key::label_cast_shadow), &cast_shadow))
						{
							geometry_comp->SetCastShadow(cast_shadow);
							editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<GeometryComponent>());
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<Collider3DComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<Collider3DComponent>::display_name);

					if (!remove_component && component_open)
					{
						if (DrawEnumCombo(EditorText(editor_key::label_type), collider_3d_comp->shape_type))
						{
							collider_3d_comp->SetDirty();
						}

						bool enabled = collider_3d_comp->IsEnabled();
						if (ImGui::Checkbox(EditorText(editor_key::label_enabled), &enabled))
						{
							collider_3d_comp->SetEnabled(enabled);
						}

						bool trigger = collider_3d_comp->IsTrigger();
						if (ImGui::Checkbox(EditorText(editor_key::label_trigger), &trigger))
						{
							collider_3d_comp->SetTrigger(trigger);
						}

						float offset[3] = { collider_3d_comp->offset.x, collider_3d_comp->offset.y, collider_3d_comp->offset.z };
						if (ImGui::InputFloat3(EditorText(editor_key::label_offset), offset))
						{
							collider_3d_comp->offset = { offset[0], offset[1], offset[2] };
							collider_3d_comp->SetDirty();
						}

						float half_extent[3] = { collider_3d_comp->half_extent.x, collider_3d_comp->half_extent.y, collider_3d_comp->half_extent.z };
						if (ImGui::InputFloat3(EditorText(editor_key::label_half_extent), half_extent))
						{
							collider_3d_comp->half_extent = {
								(std::max)(0.0f, half_extent[0]),
								(std::max)(0.0f, half_extent[1]),
								(std::max)(0.0f, half_extent[2])
							};
							collider_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(EditorText(editor_key::label_radius), &collider_3d_comp->radius))
						{
							collider_3d_comp->radius = (std::max)(0.0f, collider_3d_comp->radius);
							collider_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(EditorText(editor_key::label_friction), &collider_3d_comp->friction))
						{
							collider_3d_comp->friction = (std::max)(0.0f, collider_3d_comp->friction);
							collider_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(EditorText(editor_key::label_restitution), &collider_3d_comp->restitution))
						{
							collider_3d_comp->restitution = (std::max)(0.0f, (std::min)(1.0f, collider_3d_comp->restitution));
							collider_3d_comp->SetDirty();
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<Rigidbody3DComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<Rigidbody3DComponent>::display_name);

					if (!remove_component && component_open)
					{
						if (DrawEnumCombo(EditorText(editor_key::label_motion_type), rigidbody_3d_comp->motion_type))
						{
							rigidbody_3d_comp->SetDirty();
						}

						bool enabled = rigidbody_3d_comp->IsEnabled();
						if (ImGui::Checkbox(EditorText(editor_key::label_enabled), &enabled))
						{
							rigidbody_3d_comp->SetEnabled(enabled);
						}

						if (ImGui::InputFloat(EditorText(editor_key::label_mass), &rigidbody_3d_comp->mass))
						{
							rigidbody_3d_comp->mass = (std::max)(0.001f, rigidbody_3d_comp->mass);
							rigidbody_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(EditorText(editor_key::label_gravity_factor), &rigidbody_3d_comp->gravity_factor))
						{
							rigidbody_3d_comp->SetDirty();
						}

						float linear_vel[3] = { rigidbody_3d_comp->linear_velocity.x, rigidbody_3d_comp->linear_velocity.y, rigidbody_3d_comp->linear_velocity.z };
						if (ImGui::InputFloat3(EditorText(editor_key::label_linear_velocity), linear_vel))
						{
							rigidbody_3d_comp->linear_velocity = { linear_vel[0], linear_vel[1], linear_vel[2] };
							rigidbody_3d_comp->SetDirty();
						}

						float angular_vel[3] = { rigidbody_3d_comp->angular_velocity.x, rigidbody_3d_comp->angular_velocity.y, rigidbody_3d_comp->angular_velocity.z };
						if (ImGui::InputFloat3(EditorText(editor_key::label_angular_velocity), angular_vel))
						{
							rigidbody_3d_comp->angular_velocity = { angular_vel[0], angular_vel[1], angular_vel[2] };
							rigidbody_3d_comp->SetDirty();
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<AudioSourceComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<AudioSourceComponent>::display_name);

					if (!remove_component && component_open)
					{
						bool enabled = audio_source_comp->IsEnabled();
						if (ImGui::Checkbox(EditorText(editor_key::label_enabled), &enabled))
						{
							audio_source_comp->SetEnabled(enabled);
						}

						char sound_path[1024];
						strncpy(sound_path, audio_source_comp->sound_asset_path.c_str(), sizeof(sound_path) - 1);
						sound_path[sizeof(sound_path) - 1] = '\0';
						if (ImGui::InputText(EditorText(editor_key::label_sound_asset_path), sound_path, sizeof(sound_path)))
						{
							audio_source_comp->sound_asset_path = sound_path;
							audio_source_comp->SetDirty();
						}

						if (ImGui::SliderFloat(EditorText(editor_key::label_volume), &audio_source_comp->volume, 0.0f, 2.0f))
						{
							audio_source_comp->volume = (std::max)(0.0f, audio_source_comp->volume);
						}

						if (ImGui::SliderFloat(EditorText(editor_key::label_pitch), &audio_source_comp->pitch, 0.01f, 4.0f))
						{
							audio_source_comp->pitch = (std::max)(0.001f, audio_source_comp->pitch);
						}

						bool is_3d = audio_source_comp->Is3D();
						if (ImGui::Checkbox(EditorText(editor_key::label_is_3d), &is_3d))
						{
							audio_source_comp->Set3D(is_3d);
						}

						if (is_3d)
						{
							if (ImGui::InputFloat(EditorText(editor_key::label_min_distance), &audio_source_comp->min_distance))
							{
								audio_source_comp->min_distance = (std::max)(0.001f, audio_source_comp->min_distance);
								audio_source_comp->SetDirty();
							}
							if (ImGui::InputFloat(EditorText(editor_key::label_max_distance), &audio_source_comp->max_distance))
							{
								audio_source_comp->max_distance = (std::max)(audio_source_comp->min_distance, audio_source_comp->max_distance);
								audio_source_comp->SetDirty();
							}
						}

						bool loop = audio_source_comp->IsLoop();
						if (ImGui::Checkbox(EditorText(editor_key::label_loop), &loop))
						{
							audio_source_comp->SetLoop(loop);
						}

						bool play_on_start = audio_source_comp->IsPlayOnStart();
						if (ImGui::Checkbox(EditorText(editor_key::label_play_on_start), &play_on_start))
						{
							audio_source_comp->SetPlayOnStart(play_on_start);
						}

						bool is_playing = audio_source_comp->IsPlaying();
						if (ImGui::Checkbox(EditorText(editor_key::label_is_playing), &is_playing))
						{
							audio_source_comp->SetPlaying(is_playing);
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<AudioListenerComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<AudioListenerComponent>::display_name);

					if (!remove_component && component_open)
					{
						ImGui::Checkbox(EditorText(editor_key::label_enabled), &audio_listener_comp->enabled);
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<AudioListenerComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

					RectTransform2DComponent* rect_2d_comp = editor_viewport.view->scene->GetComponent<RectTransform2DComponent>(editor_viewport.picked_entity);
					if (rect_2d_comp)
					{
					ImGui::PushID("RectTransform2DComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<RectTransform2DComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<RectTransform2DComponent>::display_name);

					if (!remove_component && component_open)
					{
						float anchor[2] = { rect_2d_comp->anchor.x, rect_2d_comp->anchor.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_anchor), anchor))
						{
							rect_2d_comp->anchor = { anchor[0], anchor[1] };
							rect_2d_comp->SetDirty();
						}

						float position[2] = { rect_2d_comp->position.x, rect_2d_comp->position.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_position), position))
						{
							rect_2d_comp->position = { position[0], position[1] };
							rect_2d_comp->SetDirty();
						}

						float size[2] = { rect_2d_comp->size.x, rect_2d_comp->size.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_size), size))
						{
							rect_2d_comp->size = { size[0], size[1] };
							rect_2d_comp->SetDirty();
						}

						float pivot[2] = { rect_2d_comp->pivot.x, rect_2d_comp->pivot.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_pivot), pivot))
						{
							rect_2d_comp->pivot = { pivot[0], pivot[1] };
							rect_2d_comp->SetDirty();
						}

						ImGui::Text(EditorText(editor_key::label_anchor_presets));
						const struct { const char* label; float x; float y; } anchor_preset_grid[9] = {
							{ "TL", 0.0f, 0.0f }, { "TC", 0.5f, 0.0f }, { "TR", 1.0f, 0.0f },
							{ "ML", 0.0f, 0.5f }, { "MC", 0.5f, 0.5f }, { "MR", 1.0f, 0.5f },
							{ "BL", 0.0f, 1.0f }, { "BC", 0.5f, 1.0f }, { "BR", 1.0f, 1.0f },
						};
						for (int preset_index = 0; preset_index < 9; ++preset_index)
						{
							if (preset_index % 3 != 0)
							{
								ImGui::SameLine();
							}
							if (ImGui::Button(anchor_preset_grid[preset_index].label, ImVec2(34.0f, 0.0f)))
							{
								rect_2d_comp->anchor = { anchor_preset_grid[preset_index].x, anchor_preset_grid[preset_index].y };
								rect_2d_comp->pivot = { anchor_preset_grid[preset_index].x, anchor_preset_grid[preset_index].y };
								rect_2d_comp->position = { 0.0f, 0.0f };
								rect_2d_comp->SetDirty();
							}
						}
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<RectTransform2DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				ButtonComponent* button_comp = editor_viewport.view->scene->GetComponent<ButtonComponent>(editor_viewport.picked_entity);
				if (button_comp)
				{
					ImGui::PushID("ButtonComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<ButtonComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<ButtonComponent>::display_name);

					if (!remove_component && component_open)
					{
						ImGui::Checkbox(EditorText(editor_key::label_enabled), &button_comp->enabled);
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<ButtonComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				LayoutComponent* layout_comp = editor_viewport.view->scene->GetComponent<LayoutComponent>(editor_viewport.picked_entity);
				if (layout_comp)
				{
					ImGui::PushID("LayoutComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<LayoutComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<LayoutComponent>::display_name);

					if (!remove_component && component_open)
					{
						DrawEnumCombo(EditorText(editor_key::label_type), layout_comp->type);

						float padding_min[2] = { layout_comp->padding_min.x, layout_comp->padding_min.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_padding_min), padding_min))
						{
							layout_comp->padding_min = { padding_min[0], padding_min[1] };
						}

						float padding_max[2] = { layout_comp->padding_max.x, layout_comp->padding_max.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_padding_max), padding_max))
						{
							layout_comp->padding_max = { padding_max[0], padding_max[1] };
						}

						ImGui::InputFloat(EditorText(editor_key::label_spacing), &layout_comp->spacing);

						DrawEnumCombo(EditorText(editor_key::label_cross_align), layout_comp->cross_align);

						ImGui::Checkbox(EditorText(editor_key::label_reverse), &layout_comp->reverse);
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<LayoutComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Sprite2DComponent* sprite_2d_comp = editor_viewport.view->scene->GetComponent<Sprite2DComponent>(editor_viewport.picked_entity);
				if (sprite_2d_comp)
				{
					ImGui::PushID("Sprite2DComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<Sprite2DComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<Sprite2DComponent>::display_name);

					if (!remove_component && component_open)
					{
						float uv_rect[4] = { sprite_2d_comp->uv_rect.x, sprite_2d_comp->uv_rect.y, sprite_2d_comp->uv_rect.z, sprite_2d_comp->uv_rect.w };
						if (ImGui::InputFloat4(EditorText(editor_key::label_uv_rect), uv_rect))
						{
							sprite_2d_comp->uv_rect = { uv_rect[0], uv_rect[1], uv_rect[2], uv_rect[3] };
							sprite_2d_comp->SetDirty();
						}

						if (ImGui::InputInt(EditorText(editor_key::label_layer), &sprite_2d_comp->layer))
						{
							sprite_2d_comp->SetDirty();
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<Text2DComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<Text2DComponent>::display_name);

					if (!remove_component && component_open)
					{
						{
							String asset_label = text_2d_comp->font_asset_path.empty() ? String(EditorText(editor_key::label_none_placeholder)) : text_2d_comp->font_asset_path;
							ImGui::TextUnformatted(EditorText(editor_key::label_font));
							ImGui::SetNextItemWidth(-1.0f);
							if (ImGui::BeginCombo("##font", asset_label.c_str()))
							{
								if (ImGui::Selectable(EditorText(editor_key::label_none_placeholder), text_2d_comp->font_asset_path.empty()))
								{
									text_2d_comp->font_asset_path.clear();
									text_2d_comp->font = nullptr;
									text_2d_comp->SetDirty();
								}
								for (const ContentBrowserAsset& asset : content_browser.assets)
								{
									if (asset.type != ContentAssetType::Font)
									{
										continue;
									}
									const String asset_rel = io::GetRelativePath(contents_root_dir, asset.disk_path);
									if (asset_rel.empty())
									{
										continue;
									}
									const bool asset_selected = asset_rel == text_2d_comp->font_asset_path;
									if (ImGui::Selectable(asset.virtual_path.c_str(), asset_selected))
									{
										text_2d_comp->font_asset_path = asset_rel;
										text_2d_comp->font = resource::LoadFontFile(asset.disk_path);
										text_2d_comp->SetDirty();
									}
									if (asset_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
								}
							ImGui::EndCombo();
							}
						}

						char text_buf[4096] = {};
						strncpy_s(text_buf, text_2d_comp->text.c_str(), sizeof(text_buf) - 1);
						if (ImGui::InputTextMultiline(EditorText(editor_key::label_text), text_buf, sizeof(text_buf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
						{
							text_2d_comp->text = text_buf;
							text_2d_comp->SetDirty();
						}

						int pixel_height = static_cast<int>(text_2d_comp->pixel_height);
						if (ImGui::InputInt(EditorText(editor_key::label_pixel_height), &pixel_height))
						{
							text_2d_comp->pixel_height = static_cast<uint32>((std::max)(1, pixel_height));
							text_2d_comp->SetDirty();
						}

						if (ImGui::InputInt(EditorText(editor_key::label_layer), &text_2d_comp->layer))
						{
							text_2d_comp->SetDirty();
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<Sprite3DComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<Sprite3DComponent>::display_name);

					if (!remove_component && component_open)
					{
						float size[2] = { sprite_3d_comp->size.x, sprite_3d_comp->size.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_size), size))
						{
							sprite_3d_comp->size = { size[0], size[1] };
							sprite_3d_comp->SetDirty();
						}

						float pivot[2] = { sprite_3d_comp->pivot.x, sprite_3d_comp->pivot.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_pivot), pivot))
						{
							sprite_3d_comp->pivot = { pivot[0], pivot[1] };
							sprite_3d_comp->SetDirty();
						}

						float uv_rect[4] = { sprite_3d_comp->uv_rect.x, sprite_3d_comp->uv_rect.y, sprite_3d_comp->uv_rect.z, sprite_3d_comp->uv_rect.w };
						if (ImGui::InputFloat4(EditorText(editor_key::label_uv_rect), uv_rect))
						{
							sprite_3d_comp->uv_rect = { uv_rect[0], uv_rect[1], uv_rect[2], uv_rect[3] };
							sprite_3d_comp->SetDirty();
						}

						bool billboard = sprite_3d_comp->IsBillboard();
						if (ImGui::Checkbox(EditorText(editor_key::label_billboard), &billboard))
						{
							sprite_3d_comp->SetBillboard(billboard);
						}
					}
					else if (remove_component)
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<Text3DComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<Text3DComponent>::display_name);

					if (!remove_component && component_open)
					{
						ImGui::Text(EditorText(editor_key::format_font_format), text_3d_comp->font && text_3d_comp->font->IsValid() ? EditorText(editor_key::label_assigned) : EditorText(editor_key::label_none));

						char text_buf[4096] = {};
						strncpy_s(text_buf, text_3d_comp->text.c_str(), sizeof(text_buf) - 1);
						if (ImGui::InputTextMultiline(EditorText(editor_key::label_text), text_buf, sizeof(text_buf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
						{
							text_3d_comp->text = text_buf;
							text_3d_comp->SetDirty();
						}

						int pixel_height = static_cast<int>(text_3d_comp->pixel_height);
						if (ImGui::InputInt(EditorText(editor_key::label_pixel_height), &pixel_height))
						{
							text_3d_comp->pixel_height = static_cast<uint32>((std::max)(1, pixel_height));
							text_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat(EditorText(editor_key::label_height), &text_3d_comp->height))
						{
							text_3d_comp->height = (std::max)(0.0f, text_3d_comp->height);
							text_3d_comp->SetDirty();
						}

						float pivot[2] = { text_3d_comp->pivot.x, text_3d_comp->pivot.y };
						if (ImGui::InputFloat2(EditorText(editor_key::label_pivot), pivot))
						{
							text_3d_comp->pivot = { pivot[0], pivot[1] };
							text_3d_comp->SetDirty();
						}

						bool billboard = text_3d_comp->IsBillboard();
						if (ImGui::Checkbox(EditorText(editor_key::label_billboard), &billboard))
						{
							text_3d_comp->SetBillboard(billboard);
						}
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<Text3DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				AnimationComponent* animation_comp = FindComponent<AnimationComponent>(*editor_viewport.view->scene, editor_viewport.picked_entity);
				if (animation_comp)
				{
					ImGui::PushID("AnimationComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<AnimationComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<AnimationComponent>::display_name);

					if (!remove_component && component_open)
					{
						const int clip_count = static_cast<int>(animation_comp->clips.size());
						ImGui::Text(EditorText(editor_key::format_clips_format), clip_count);

						if (clip_count > 0)
						{
							if (animation_comp->current_clip_index >= animation_comp->clips.size())
							{
								animation_comp->current_clip_index = 0;
							}

							int current_clip_index = static_cast<int>(animation_comp->current_clip_index);
							std::shared_ptr<resource::AnimationClip> current_clip = animation_comp->clips[animation_comp->current_clip_index];
							String current_clip_name = current_clip && !current_clip->name.empty() ? current_clip->name : String(EditorText(editor_key::label_clip)) + " " + std::to_string(current_clip_index);
							if (ImGui::BeginCombo(EditorText(editor_key::label_clip), current_clip_name.c_str()))
							{
								for (int clip_index = 0; clip_index < clip_count; ++clip_index)
								{
									const std::shared_ptr<resource::AnimationClip>& clip = animation_comp->clips[clip_index];
									String clip_name = clip && !clip->name.empty() ? clip->name : String(EditorText(editor_key::label_clip)) + " " + std::to_string(clip_index);
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

							if (ImGui::Button(animation_comp->playing ? EditorText(editor_key::action_pause) : EditorText(editor_key::action_play)))
							{
								animation_comp->playing = !animation_comp->playing;
							}
							ImGui::SameLine();
							ImGui::Checkbox(EditorText(editor_key::label_loop), &animation_comp->loop);

							ImGui::DragFloat(EditorText(editor_key::label_speed), &animation_comp->speed, 0.01f, -10.0f, 10.0f);

							if (duration_seconds > 0.0f)
							{
								float time = std::clamp(animation_comp->time, 0.0f, duration_seconds);
								if (ImGui::SliderFloat(EditorText(editor_key::label_time), &time, 0.0f, duration_seconds))
								{
									animation_comp->time = time;
									animation_comp->bone_matrices_dirty = true;
								}
								ImGui::Text(EditorText(editor_key::format_duration_format), duration_seconds);

								if (!is_playing && animation_comp->playing)
								{
									const float advanced_time = animation_comp->time + ImGui::GetIO().DeltaTime * animation_comp->speed;
									animation_comp->time = animation_comp->loop ? math::Wrap(advanced_time, duration_seconds) : std::clamp(advanced_time, 0.0f, duration_seconds);
									animation_comp->bone_matrices_dirty = true;
								}
							}
							else
							{
								ImGui::TextDisabled(EditorText(editor_key::label_duration_zero));
							}

							if (current_clip)
							{
								ImGui::Separator();
								ImGui::Text(EditorText(editor_key::label_animation_events));

								ImDrawList* event_draw_list = ImGui::GetWindowDrawList();
								const ImVec2 timeline_min = ImGui::GetCursorScreenPos();
								const float timeline_width = ImGui::GetContentRegionAvail().x;
								const float timeline_height = 24.0f;
								ImGui::InvisibleButton("##animation_event_timeline", ImVec2(timeline_width, timeline_height));
								const ImVec2 timeline_max = ImVec2(timeline_min.x + timeline_width, timeline_min.y + timeline_height);
								event_draw_list->AddRectFilled(timeline_min, timeline_max, IM_COL32(40, 40, 40, 255), 3.0f);
								event_draw_list->AddRect(timeline_min, timeline_max, IM_COL32(90, 90, 90, 255), 3.0f);

								const float event_track_duration = duration_seconds > 0.0f ? duration_seconds : 1.0f;
								const float playhead_ratio = std::clamp(animation_comp->time / event_track_duration, 0.0f, 1.0f);
								const float playhead_x = timeline_min.x + playhead_ratio * timeline_width;
								event_draw_list->AddLine(ImVec2(playhead_x, timeline_min.y), ImVec2(playhead_x, timeline_max.y), IM_COL32(255, 220, 60, 255), 2.0f);

								for (const resource::AnimationEventMarker& marker : current_clip->events)
								{
									const float marker_ratio = std::clamp(marker.time_seconds / event_track_duration, 0.0f, 1.0f);
									const float marker_x = timeline_min.x + marker_ratio * timeline_width;
									event_draw_list->AddLine(ImVec2(marker_x, timeline_min.y), ImVec2(marker_x, timeline_max.y), IM_COL32(80, 180, 255, 255), 2.0f);
									event_draw_list->AddTriangleFilled(ImVec2(marker_x - 4.0f, timeline_min.y), ImVec2(marker_x + 4.0f, timeline_min.y), ImVec2(marker_x, timeline_min.y + 7.0f), IM_COL32(80, 180, 255, 255));
								}

								if (ImGui::Button(EditorText(editor_key::action_animation_event_add)))
								{
									resource::AnimationEventMarker new_marker = {};
									new_marker.time_seconds = std::clamp(animation_comp->time, 0.0f, duration_seconds);
									new_marker.name = EditorText(editor_key::label_animation_event_default_name);
									current_clip->events.push_back(new_marker);
								}
								ImGui::SameLine();
								if (ImGui::Button(EditorText(editor_key::action_animation_event_save)))
								{
									GeometryComponent* event_geometry = editor_viewport.view->scene->GetComponent<GeometryComponent>(editor_viewport.picked_entity);
									if (event_geometry && event_geometry->mesh && !event_geometry->mesh_asset_path.empty())
									{
										const String event_disk_path = project::ResolveProjectContentPath(contents_root_dir, event_geometry->mesh_asset_path);
										String event_binary_path;
										resource::AssetMeta event_asset_meta = {};
										if (won::utils::ToLower(io::GetExtension(event_disk_path)) == resource::mesh_binary_extension)
										{
											event_binary_path = event_disk_path;
										}
										else if (resource::LoadAssetMeta(resource::GetAssetMetaPath(event_disk_path), event_asset_meta))
										{
											event_binary_path = project::ResolveProjectContentPath(contents_root_dir, event_asset_meta.binary_path);
										}
										if (!event_binary_path.empty() && resource::SaveMeshBinary(event_binary_path, *event_geometry->mesh))
										{
											backlog::Post(String(EditorText(editor_key::message_animation_events_saved)) + event_binary_path, backlog::LogLevel::Default);
										}
										else
										{
											backlog::Post(EditorText(editor_key::message_animation_events_save_failed), backlog::LogLevel::Warning);
										}
									}
									else
									{
										backlog::Post(EditorText(editor_key::message_animation_events_no_mesh), backlog::LogLevel::Warning);
									}
								}

								int marker_index_to_remove = -1;
								for (int marker_index = 0; marker_index < static_cast<int>(current_clip->events.size()); ++marker_index)
								{
									resource::AnimationEventMarker& marker = current_clip->events[marker_index];
									ImGui::PushID(marker_index);
									ImGui::SetNextItemWidth(90.0f);
									float marker_time = marker.time_seconds;
									if (ImGui::DragFloat("##animation_event_time", &marker_time, 0.01f, 0.0f, duration_seconds, "%.3f"))
									{
										marker.time_seconds = std::clamp(marker_time, 0.0f, duration_seconds);
									}
									ImGui::SameLine();
									char marker_name_buffer[128] = {};
									std::snprintf(marker_name_buffer, sizeof(marker_name_buffer), "%s", marker.name.c_str());
									ImGui::SetNextItemWidth(150.0f);
									if (ImGui::InputText("##animation_event_name", marker_name_buffer, sizeof(marker_name_buffer)))
									{
										marker.name = marker_name_buffer;
									}
									ImGui::SameLine();
									if (ImGui::Button(EditorText(editor_key::action_animation_event_remove)))
									{
										marker_index_to_remove = marker_index;
									}
									ImGui::PopID();
								}
								if (marker_index_to_remove >= 0)
								{
									current_clip->events.erase(current_clip->events.begin() + marker_index_to_remove);
								}
							}
						}
						else
						{
							ImGui::TextDisabled(EditorText(editor_key::label_no_animation_clips));
						}

						ImGui::Text(EditorText(editor_key::format_bone_matrices_format), static_cast<int>(animation_comp->bone_matrices.size()));
						ImGui::Text(EditorText(editor_key::format_bone_matrix_offset_format), animation_comp->bone_matrix_offset);
					}
					else if (remove_component)
					{
						const ecs::Entity entity = editor_viewport.picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](const won::function::Value&) {
							editor_viewport.view->scene->RemoveComponent<AnimationComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				MaterialComponent* material_comp = FindComponent<MaterialComponent>(*editor_viewport.view->scene, editor_viewport.picked_entity);
				if (material_comp)
				{
					ImGui::PushID("MaterialComponent");
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<MaterialComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<MaterialComponent>::display_name);

					if (!remove_component && component_open)
					{
						static Entity selected_material_entity = INVALID_ENTITY;
						static int selected_material_slot = 0;
						if (selected_material_entity != editor_viewport.picked_entity)
						{
							selected_material_entity = editor_viewport.picked_entity;
							selected_material_slot = 0;
						}

						{
							String asset_label = material_comp->material_asset_path.empty() ? String(EditorText(editor_key::label_none_placeholder)) : material_comp->material_asset_path;
							ImGui::TextUnformatted(EditorText(editor_key::label_material_asset));
							ImGui::SetNextItemWidth(-1.0f);
							if (ImGui::BeginCombo("##material_asset", asset_label.c_str()))
							{
								if (ImGui::Selectable(EditorText(editor_key::label_none_placeholder), material_comp->material_asset_path.empty()))
								{
									material_comp->SetMaterial(nullptr);
									material_comp->SetMaterialAssetPath(String());
									material_comp->SetDirty();
									editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<MaterialComponent>());
								}
								for (const ContentBrowserAsset& asset : content_browser.assets)
								{
									if (asset.type != ContentAssetType::Material)
									{
										continue;
									}
									const String asset_rel = io::GetRelativePath(contents_root_dir, asset.disk_path);
									if (asset_rel.empty())
									{
										continue;
									}
									const bool asset_selected = asset_rel == material_comp->material_asset_path;
									if (ImGui::Selectable(asset.virtual_path.c_str(), asset_selected))
									{
										material_comp->SetMaterial(resource::LoadMaterialBinary(asset.disk_path));
										material_comp->SetMaterialAssetPath(asset_rel);
										material_comp->SetDirty();
										editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<MaterialComponent>());
									}
									if (asset_selected)
									{
										ImGui::SetItemDefaultFocus();
									}
								}
							ImGui::EndCombo();
							}
						}
						int material_slot_count = static_cast<int>(material_comp->GetMaterialSlotCount());
						ImGui::Text(EditorText(editor_key::format_material_slots_format), material_slot_count);

						// Fork is meaningful when the material is shared: cache-backed (has a path) or
						// referenced by more than this component. Mirrors ForkMaterial()'s guard.
						const bool fork_meaningful = material_comp->material
							&& (!material_comp->material_asset_path.empty() || material_comp->material.use_count() > 1);
						ImGui::BeginDisabled(!fork_meaningful);
						if (ImGui::Button(EditorText(editor_key::action_fork_material)))
						{
							material_comp->ForkMaterial();
							editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<MaterialComponent>());
						}
						ImGui::EndDisabled();

						// Promote an inline (forked) material to a shared .wonmat asset.
						ImGui::SameLine();
						ImGui::BeginDisabled(!material_comp->material || !material_comp->material_asset_path.empty());
						if (ImGui::Button(EditorText(editor_key::action_save_material_asset)))
						{
							open_save_material_asset = true;
						}
						ImGui::EndDisabled();

						if (ImGui::Button(EditorText(editor_key::action_add_slot)))
						{
							material_comp->AddMaterialSlot();
							editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<MaterialComponent>());
							selected_material_slot = material_slot_count;
							material_slot_count = static_cast<int>(material_comp->GetMaterialSlotCount());
						}
						ImGui::SameLine();
						if (material_slot_count == 0)
						{
							ImGui::BeginDisabled();
						}
						if (ImGui::Button(EditorText(editor_key::action_remove_slot)) && material_slot_count > 0)
						{
							material_comp->material->slots.erase(material_comp->material->slots.begin() + selected_material_slot);
							material_comp->material->SetDirty();
							material_comp->SetDirty();
							editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<MaterialComponent>());
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

							String slot_label = String(EditorText(editor_key::label_slot_prefix)) + std::to_string(selected_material_slot);
							if (ImGui::BeginCombo(EditorText(editor_key::label_selected_slot), slot_label.c_str()))
							{
								for (int slot_index = 0; slot_index < material_slot_count; ++slot_index)
								{
									String item_label = String(EditorText(editor_key::label_slot_prefix)) + std::to_string(slot_index);
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
							material_changed |= DrawEnumCombo(EditorText(editor_key::label_shader_type), material_slot.material_type);
							material_changed |= DrawEnumCombo(EditorText(editor_key::label_blend_mode), material_slot.blend_mode);

							material_changed |= ImGui::Checkbox(EditorText(editor_key::label_double_sided), &material_slot.double_sided);
							material_changed |= ImGui::Checkbox(EditorText(editor_key::label_use_vertex_colors), &material_slot.use_vertex_colors);
							material_changed |= ImGui::Checkbox(EditorText(editor_key::label_receive_shadow), &material_slot.receive_shadow);

							float base_color[4] = { material_slot.base_color.x, material_slot.base_color.y, material_slot.base_color.z, material_slot.base_color.w };
							if (ImGui::ColorEdit4(EditorText(editor_key::label_base_color), base_color))
							{
								material_slot.base_color = { base_color[0], base_color[1], base_color[2], base_color[3] };
								material_changed = true;
							}

							float emissive_color[3] = { material_slot.emissive_color.x, material_slot.emissive_color.y, material_slot.emissive_color.z };
							if (ImGui::ColorEdit3(EditorText(editor_key::label_emissive_color), emissive_color))
							{
								material_slot.emissive_color = { emissive_color[0], emissive_color[1], emissive_color[2] };
								material_changed = true;
							}
							material_changed |= ImGui::DragFloat(EditorText(editor_key::label_emissive_intensity), &material_slot.emissive_intensity, 0.1f, 0.0f, 1000000.0f);

							material_changed |= ImGui::SliderFloat(EditorText(editor_key::label_metallic), &material_slot.metallic, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(EditorText(editor_key::label_roughness), &material_slot.roughness, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(EditorText(editor_key::label_reflectance), &material_slot.reflectance, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(EditorText(editor_key::label_anisotropy), &material_slot.anisotropy, -1.0f, 1.0f);

							float sheen_color[3] = { material_slot.sheen_color.x, material_slot.sheen_color.y, material_slot.sheen_color.z };
							if (ImGui::ColorEdit3(EditorText(editor_key::label_sheen_color), sheen_color))
							{
								material_slot.sheen_color = { sheen_color[0], sheen_color[1], sheen_color[2] };
								material_changed = true;
							}

							material_changed |= ImGui::SliderFloat(EditorText(editor_key::label_sheen_roughness), &material_slot.sheen_roughness, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(EditorText(editor_key::label_clearcoat), &material_slot.clearcoat, 0.0f, 1.0f);
							material_changed |= ImGui::SliderFloat(EditorText(editor_key::label_clearcoat_roughness), &material_slot.clearcoat_roughness, 0.0f, 1.0f);

							const char* texture_slot_names[] = {
								EditorText(editor_key::label_base_color_map),
								EditorText(editor_key::label_normal_map),
								EditorText(editor_key::label_emissive_map),
								EditorText(editor_key::label_opacity_map),
								EditorText(editor_key::label_displacement_map),
								EditorText(editor_key::label_occlusion_map),
								EditorText(editor_key::label_sheen_color_map),
								EditorText(editor_key::label_sheen_roughness_map),
								EditorText(editor_key::label_clearcoat_map),
								EditorText(editor_key::label_clearcoat_roughness_map),
								EditorText(editor_key::label_clearcoat_normal_map),
								EditorText(editor_key::label_anisotropy_map),
								EditorText(editor_key::label_roughness_map),
								EditorText(editor_key::label_metallic_map),
							};
							static_assert(TEXTURESLOT_COUNT == IM_ARRAYSIZE(texture_slot_names), "Texture slot labels must match");

							ImGui::SeparatorText(EditorText(editor_key::label_textures));
							for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
							{
								resource::MaterialSlot::TextureMap& texture = material_slot.textures[texture_slot];
								ImGui::PushID(static_cast<int>(texture_slot));
								String texture_label = texture.texture_asset_path.empty() ? String(EditorText(editor_key::label_none_placeholder)) : texture.texture_asset_path;
								ImGui::TextUnformatted(texture_slot_names[texture_slot]);
								ImGui::SetNextItemWidth(-1.0f);
								if (ImGui::BeginCombo("##texture", texture_label.c_str()))
								{
									if (ImGui::Selectable(EditorText(editor_key::label_none_placeholder), texture.texture_asset_path.empty()))
									{
										texture.texture_asset_path.clear();
										texture.image = nullptr;
										material_changed = true;
									}
									for (const ContentBrowserAsset& asset : content_browser.assets)
									{
										if (asset.type != ContentAssetType::Texture)
										{
											continue;
										}
										if (won::utils::ToLower(io::GetExtension(asset.disk_path)) != resource::texture_binary_extension)
										{
											continue;
										}
										const String texture_rel = io::GetRelativePath(contents_root_dir, asset.disk_path);
										if (texture_rel.empty())
										{
											continue;
										}
										const bool texture_selected = texture_rel == texture.texture_asset_path;
										if (ImGui::Selectable(asset.virtual_path.c_str(), texture_selected))
										{
											auto image = resource::LoadTextureBinary(asset.disk_path);
											if (image && image->IsValid() && rendering::utils::CreateRenderData(*device, *image, image->format, false))
											{
												texture.texture_asset_path = texture_rel;
												texture.image = image;
												material_changed = true;
											}
										}
										if (texture_selected)
										{
											ImGui::SetItemDefaultFocus();
										}
									}
									ImGui::EndCombo();
								}
								ImGui::PopID();
							}

							if (material_changed)
							{
								material_comp->material->SetDirty();
								material_comp->SetDirty();
								editor_viewport.view->scene->MarkGpuDirty(ComponentMaskFromType<MaterialComponent>());
							}
						}
					}
					else if (remove_component)
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
										if (material_slot.textures[texture_slot].image && material_slot.textures[texture_slot].image->render_data.texture)
										{
											deferred_res_removal.resources.push_back(material_slot.textures[texture_slot].image->render_data.texture);
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
					const bool component_open = DrawComponentCollapsingHeader(reflection::TypeMeta<ScriptComponent>::display_name);
					bool remove_component = DrawComponentRemoveButton(reflection::TypeMeta<ScriptComponent>::display_name);
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

					if (!remove_component && component_open)
					{
						bool enabled = script_comp->enabled;
						if (ImGui::Checkbox(EditorText(editor_key::label_enabled), &enabled))
						{
							script_comp->enabled = enabled;
						}

						for (Size script_index = 0; script_index < script_comp->scripts.size();)
						{
							ScriptSlot& script_slot = script_comp->scripts[script_index];
							ImGui::PushID(static_cast<int>(script_index));

							bool slot_enabled = script_slot.enabled;
							if (ImGui::Checkbox(EditorText(editor_key::label_script_enabled), &slot_enabled))
							{
								script_slot.enabled = slot_enabled;
							}

							String script_label = EditorText(editor_key::label_none_placeholder);
							if (!script_slot.script_path.empty())
							{
								script_label = project::MakeVirtualContentPath(script_slot.script_path);
							}

							ImGui::SetNextItemWidth(-1.0f);
							if (ImGui::BeginCombo(EditorText(editor_key::label_script), script_label.c_str()))
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
											script_slot.last_error = EditorText(editor_key::label_script_already_exists);
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
											script_slot.last_error = EditorText(editor_key::label_script_already_exists);
										}
									}
								}
								ImGui::EndDragDropTarget();
							}

							char path_buf[512];
							std::snprintf(path_buf, sizeof(path_buf), "%s", script_slot.script_path.c_str());
							if (ImGui::InputText(EditorText(editor_key::label_path), path_buf, sizeof(path_buf)))
							{
								String new_path = io::NormalizePath(path_buf);
								if (project::IsVirtualContentPath(new_path))
								{
									new_path = project::StripVirtualContentRoot(new_path);
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
									script_slot.last_error = EditorText(editor_key::label_script_already_exists);
								}
							}

							if (ImGui::Button(EditorText(editor_key::action_reload), ImVec2(DEFAULTBUTTONWIDTH, 0)))
							{
								reload_script(editor_viewport.picked_entity, script_slot);
							}

							ImGui::SameLine();
							const bool remove_script = ImGui::Button(EditorText(editor_key::action_remove), ImVec2(DEFAULTBUTTONWIDTH, 0));
							ImGui::SameLine();
							if (script_index == 0)
							{
								ImGui::BeginDisabled();
							}
							const bool move_up = ImGui::Button(EditorText(editor_key::action_up), ImVec2(DEFAULTBUTTONWIDTH, 0));
							if (script_index == 0)
							{
								ImGui::EndDisabled();
							}
							ImGui::SameLine();
							if (script_index + 1 >= script_comp->scripts.size())
							{
								ImGui::BeginDisabled();
							}
							const bool move_down = ImGui::Button(EditorText(editor_key::action_down), ImVec2(DEFAULTBUTTONWIDTH, 0));
							if (script_index + 1 >= script_comp->scripts.size())
							{
								ImGui::EndDisabled();
							}

							if (!script_slot.last_error.empty())
							{
								ImGui::TextWrapped(EditorText(editor_key::format_last_error_format), script_slot.last_error.c_str());
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

						if (ImGui::Button(EditorText(editor_key::action_add_script), ImVec2(DEFAULTBUTTONWIDTH, 0)))
						{
							script_comp->scripts.push_back({});
						}
					}
					else if (remove_component)
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
						if (ImGui::Button(EditorText(editor_key::action_regenerate_terrain)))
						{
							GeometryComponent* geometry = editor_viewport.view->scene->GetComponent<GeometryComponent>(editor_viewport.picked_entity);
							if (terrain && geometry && device)
							{
								auto terrain_mesh = GenerateTerrainMesh(*terrain);
								if (terrain_mesh && terrain_mesh->IsValid())
								{
									const String terrain_rel_path = String(editor_asset_path::generated_directory) + "/terrain_" + std::to_string(editor_viewport.picked_entity) + "." + resource::mesh_binary_extension;
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
					else if (type_desc->type_id == reflection::TypeMeta<NavMeshComponent>::type_id)
					{
						ImGui::PushID("NavMeshBake");
						NavMeshComponent* nav = editor_viewport.view->scene->GetComponent<NavMeshComponent>(editor_viewport.picked_entity);
						if (nav && ImGui::Button(EditorText(editor_key::action_bake_navmesh)))
						{
							if (nav->navmesh_asset_path.empty())
							{
								nav->navmesh_asset_path = String(editor_asset_path::generated_directory) + "/navmesh_" + std::to_string(editor_viewport.picked_entity) + "." + resource::navmesh_binary_extension;
							}
							const bool baked = resource::BuildSceneNavMesh(*editor_viewport.view->scene, contents_root_dir);
							backlog::Post(String(EditorText(editor_key::action_bake_navmesh)) + (baked ? ": " + nav->navmesh_asset_path : " failed"), baked ? backlog::LogLevel::Default : backlog::LogLevel::Warning);
						}
						ImGui::PopID();
					}
				}

				if (ImGui::Button(EditorText(editor_key::action_add_component), ImVec2(-1.0f, 0.0f)))
				{
					ImGui::OpenPopup(editor_popup_id::add_component);
				}

				if (ImGui::BeginPopup(editor_popup_id::add_component))
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
							void* component = AddComponentWithCompanions(editor_viewport.view->scene, editor_viewport.picked_entity, type_desc);
							if (component && type_desc->type_id == reflection::TypeMeta<NameComponent>::type_id)
							{
								static_cast<NameComponent*>(component)->value = "Entity " + std::to_string(editor_viewport.picked_entity);
							}
						}
						has_component_item = true;
					}
					if (!has_component_item)
					{
						ImGui::TextDisabled(EditorText(editor_key::label_no_components_available));
					}

					ImGui::EndPopup();
				}
			}
		}
		ImGui::End();

		UpdateInspectorHistory();

		if (ImGui::Begin(EditorLabel(editor_key::window_contents_browser, editor_window_id::contents_browser), nullptr))
		{
			DrawContentsBrowser();
		}
		ImGui::End();

		if (ImGui::Begin(EditorLabel(editor_key::window_log, editor_window_id::log), nullptr, ImGuiWindowFlags_NoScrollbar))
		{
			const std::string log = won::backlog::GetText();

			//ImGui::SameLine();
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));
			if (ImGui::Button(EditorText(editor_key::action_copy_to_clipboard), ImVec2(DEFAULTBUTTONWIDTH, 0)))
				ImGui::SetClipboardText(log.c_str());
			ImGui::SameLine();
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));
			if (ImGui::Button(EditorText(editor_key::action_clear), ImVec2(DEFAULTBUTTONWIDTH, 0)))
				won::backlog::Clear();

			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, 3));

			ImGui::BeginChild("##log", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_AlwaysVerticalScrollbar);
			ImGui::Text("%s", log.c_str());

			if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
				ImGui::SetScrollHereY(1.0f);
			ImGui::EndChild();

		}
		ImGui::End();

		if (ImGui::Begin(EditorLabel(editor_key::window_profiler, editor_window_id::profiler), nullptr, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));

			static bool profiler_enabled = false;
			static double last_profiler_ui_update_time = -1.0;
			static std::string cached_performance;
			static std::string cached_res_usage;
			if (ImGui::Checkbox(EditorText(editor_key::label_enable_profiler), &profiler_enabled))
			{
				profiler::SetEnabled(profiler_enabled);
				last_profiler_ui_update_time = -1.0;
				cached_performance = profiler_enabled ? EditorText(editor_key::label_profiler_starting) : "";
				cached_res_usage.clear();
			}

			if (profiler_enabled)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), EditorText(editor_key::label_profiler_warning));

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
			ImGui::SetWindowFocus(editor_window_id::contents_browser);
			focus_contents_browser_on_startup = false;
		}

		DrawBackgroundTaskStatus();

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


		RHISubresourceBinding back_buffer_binding = {};
		if (!renderer->GetCurrentBackBufferBinding(back_buffer_binding))
		{
			return;
		}

		rendering::FrameGraph& frame_graph = renderer->GetFrameGraph();
		const rendering::FrameResourceId back_buffer_id = frame_graph.Import(*back_buffer_binding.resource);
		frame_graph.AddPass("Draw ImGui",
			{ { back_buffer_id, RHIResourceState::RenderTarget, rendering::FrameResourceAccess::Type::ReadWrite } },
			[this, drawData, fb_width, fb_height, back_buffer_binding](const rendering::FrameGraphPassContext& pass_context) {

			RHICommandList* command_list = pass_context.command_list;
			FrameContext& frame_context = renderer->GetFrameContext();
			Vector<RHISubresourceBinding> color_targets = { back_buffer_binding };

			FrameUploadAllocation allocation{};
			auto gpu_range = profiler::ScopedRangeGPU("Draw ImGui", *command_list);

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
				cb_binding.resource = allocation.buffer;
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

		Application::RenderUI();
	}

	bool EditorApplication::RebuildImGuiFont(bool wait_for_gpu)
	{
		if (wait_for_gpu)
		{
			WaitIdle();
		}

		ImGuiIO& io = ImGui::GetIO();
		const String language = editor_locale.GetLanguage();
		AddImGuiFont(
			GetEditorFontFolder(language),
			GetEditorFontFileName(language),
			GetEditorFontGlyphRanges(language));

		unsigned char* pixels = nullptr;
		int width = 0;
		int height = 0;
		io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);

		rendering::RHITextureDesc texture_desc;
		texture_desc.width = width;
		texture_desc.height = height;
		texture_desc.mip_levels = 1;
		texture_desc.array_layers = 1;
		texture_desc.format = RHIFormat::R8G8B8A8Unorm;
		texture_desc.bind_flags = RHIBindFlags::ShaderResource;

		std::unique_ptr<RHIResource> new_font = device->CreateTexture(texture_desc, pixels, width * height * 4);
		if (!new_font)
		{
			return false;
		}

		RHISubresourceDesc subresource_desc;
		subresource_desc.type = RHISubresourceType::ShaderResource;
		RHISubresourceHandle new_font_subresource;
		if (!device->CreateSubresource(*new_font, subresource_desc, &new_font_subresource))
		{
			return false;
		}

		std::shared_ptr<RHIResource> old_font = std::move(imgui_font);
		imgui_font = std::move(new_font);
		imgui_font_subresource = new_font_subresource;
		io.Fonts->SetTexID((ImTextureID)imgui_font.get());
		old_font.reset();
		return true;
	}

	void EditorApplication::InitImGui()
	{
		RebuildImGuiFont(false);

		RHISamplerDesc sampler_desc;
		sampler_desc.address_u = RHIAddressMode::Wrap;
		sampler_desc.address_v = RHIAddressMode::Wrap;
		sampler_desc.address_w = RHIAddressMode::Wrap;
		imgui_sampler = device->CreateSampler(sampler_desc);

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

		ApplyEditorCameraExposure();
	}

	void EditorApplication::ApplyEditorCameraExposure()
	{
		if (!editor_viewport.view || !editor_viewport.view->scene || editor_viewport.view->camera_entity == ecs::INVALID_ENTITY)
		{
			return;
		}

		auto camera = editor_viewport.view->scene->GetComponent<ecs::CameraComponent>(editor_viewport.view->camera_entity);
		if (!camera)
		{
			return;
		}

		camera->SetAutoExposure(editor_settings.editor_camera_auto_exposure);
		const float auto_exposure_min_ev = (std::min)(editor_settings.editor_camera_auto_exposure_min_ev, editor_settings.editor_camera_auto_exposure_max_ev);
		const float auto_exposure_max_ev = (std::max)(editor_settings.editor_camera_auto_exposure_min_ev, editor_settings.editor_camera_auto_exposure_max_ev);
		editor_settings.editor_camera_auto_exposure_min_ev = auto_exposure_min_ev;
		editor_settings.editor_camera_auto_exposure_max_ev = auto_exposure_max_ev;
		camera->auto_exposure_min_ev = auto_exposure_min_ev;
		camera->auto_exposure_max_ev = auto_exposure_max_ev;
		camera->auto_exposure_speed = (std::max)(0.0f, editor_settings.editor_camera_auto_exposure_speed);

		if (camera->IsAutoExposure())
		{
			camera->exposure_compensation = editor_settings.editor_camera_exposure_compensation;
		}
		else
		{
			const float physical_exposure = camera->GetPhysicalExposure();
			const float fixed_exposure = ecs::CameraComponent::ExposureFromEV100(editor_settings.editor_camera_fixed_ev100);
			camera->exposure_multiplier = physical_exposure;
			camera->exposure_compensation = std::log2(fixed_exposure / physical_exposure) + editor_settings.editor_camera_exposure_compensation;
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
		editor_history.Clear();
		ResetInspectorBaseline();
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
			backlog::Post(EditorText(editor_key::message_create_project_failed) + project_root, backlog::LogLevel::Warning);
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
		new_project_settings.startup_scene = String(editor_asset_path::scene_directory) + "/" + editor_asset_path::default_scene_file + "." + resource::scene_file_extension;
		new_project_settings.window_title = new_project_settings.project_name;
		new_project_settings.splash_title = new_project_settings.project_name;

		const String new_content_root = project::GetContentRoot(new_project_settings);
		if (!io::CreateDirectories(new_content_root) ||
			!io::CreateDirectories(io::CombinePath(new_content_root, editor_asset_path::scene_directory)) ||
			!io::CreateDirectories(io::CombinePath(new_content_root, "Images")))
		{
			backlog::Post(EditorText(editor_key::message_create_project_failed) + new_content_root, backlog::LogLevel::Warning);
			return false;
		}

		if (!project::SaveSettings(settings_path, new_project_settings))
		{
			backlog::Post(EditorText(editor_key::message_create_project_failed) + settings_path, backlog::LogLevel::Warning);
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
		backlog::Post(EditorText(editor_key::message_project_created) + settings_path);
		return true;
	}

	bool EditorApplication::LoadProject(const String& path)
	{
		if (path.empty())
		{
			return false;
		}

		if (is_playing)
		{
			ExitPlay();
		}

		project::ProjectSettings project_settings_to_load = {};
		const String settings_path = io::NormalizePath(path);
		if (!io::IsFile(settings_path) || !project::LoadSettings(settings_path, project_settings_to_load))
		{
			backlog::Post(EditorText(editor_key::message_load_project_failed) + settings_path, backlog::LogLevel::Warning);
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
		ApplyProjectSettings(loaded_project_settings);
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
		scene_desc.enable_simulation = false;
		ecs::Scene* old_scene = editor_viewport.view ? editor_viewport.view->scene : nullptr;
		ecs::Scene& new_scene = GetSceneManager()->CreateScene(scene_desc);
		edit_scene = &new_scene;
		if (editor_viewport.view)
		{
			editor_viewport.view->scene = &new_scene;
			editor_viewport.view->camera_entity = ecs::INVALID_ENTITY;
		}
		GetSceneManager()->DestroyScene(old_scene);
		current_scene_path.clear();
		editor_viewport.picked_entity = ecs::INVALID_ENTITY;
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
		backlog::Post(EditorText(editor_key::message_project_loaded) + settings_path);
		return true;
	}

	bool EditorApplication::SaveProject()
	{
		if (loaded_project_settings.settings_path.empty())
		{
			backlog::Post(EditorText(editor_key::message_save_project_failed) + String(EditorText(editor_key::message_package_project_missing_settings)), backlog::LogLevel::Warning);
			return false;
		}

		const String settings_directory = io::GetDirectoryFromPath(loaded_project_settings.settings_path);
		if (!settings_directory.empty() && !io::CreateDirectories(settings_directory))
		{
			backlog::Post(EditorText(editor_key::message_save_project_failed) + settings_directory, backlog::LogLevel::Warning);
			return false;
		}

		if (!project::SaveSettings(loaded_project_settings.settings_path, loaded_project_settings))
		{
			backlog::Post(EditorText(editor_key::message_save_project_failed) + loaded_project_settings.settings_path, backlog::LogLevel::Warning);
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

		backlog::Post(EditorText(editor_key::message_project_saved) + loaded_project_settings.settings_path);
		return true;
	}

	void EditorApplication::ReloadLocalizationTables()
	{
		LocalizationEditorState& state = localization_editor;
		state.languages.clear();
		state.tables.clear();
		state.keys.clear();
		state.dirty = false;
		state.loaded = true;

		for (const String& code : loaded_project_settings.packaged_languages)
		{
			const String normalized = locale::NormalizeLanguage(code);
			if (normalized.empty())
			{
				continue;
			}
			state.languages.push_back(normalized);
			locale::Table table;
			locale::LoadTable(locale::GetTablePath(normalized), table);
			state.tables.emplace(normalized, std::move(table));
		}

		UnorderedSet<String> unique_keys;
		for (const auto& pair : state.tables)
		{
			for (const auto& entry : pair.second)
			{
				unique_keys.insert(entry.first);
			}
		}
		state.keys.assign(unique_keys.begin(), unique_keys.end());
		std::sort(state.keys.begin(), state.keys.end());
	}

	bool EditorApplication::SaveLocalizationTables()
	{
		LocalizationEditorState& state = localization_editor;
		const String default_language = locale::NormalizeLanguage(loaded_project_settings.default_language);

		bool saved = true;
		for (const String& code : state.languages)
		{
			locale::Table& table = state.tables[code];
			for (const String& key : state.keys)
			{
				locale::TableEntry& entry = table[key];
				const auto default_table = state.tables.find(default_language);
				if (default_table != state.tables.end())
				{
					const auto default_entry = default_table->second.find(key);
					entry.source = default_entry != default_table->second.end() ? default_entry->second.text : String();
				}
			}
			saved = locale::SaveTable(locale::GetTablePath(code), code, table) && saved;
		}
		if (saved)
		{
			state.dirty = false;
		}
		return saved;
	}

	void EditorApplication::DrawEditorPreferencesWindow(bool* open)
	{
		if (!open || !*open)
		{
			return;
		}

		if (ImGui::Begin(EditorLabel(editor_key::window_preferences, editor_window_id::editor_preferences), open))
		{
			const Vector<String>& languages = editor_locale.GetAvailableLanguages();
			const String& current = editor_locale.GetLanguage();
			ImGui::TextUnformatted(EditorText(editor_key::label_language));
			ImGui::SameLine();
			ImGui::SetNextItemWidth(200.0f);
			if (ImGui::BeginCombo("##editor_language", current.c_str()))
			{
				for (const String& language : languages)
				{
					const bool selected = language == current;
					if (ImGui::Selectable(language.c_str(), selected) && !selected)
					{
						editor_locale.SetLanguage(language);
						editor_settings.editor_language = language;
						imgui_font_reload_pending = true;
						SaveEditorSettings();
					}
					if (selected)
					{
						ImGui::SetItemDefaultFocus();
					}
				}
				ImGui::EndCombo();
			}
			ImGui::TextDisabled("%s", EditorText(editor_key::message_preferences_scope));
		}
		ImGui::End();
	}

	void EditorApplication::InitializeEditorLanguage()
	{
		const String engine_content_root = io::NormalizePath(String(CONTENTS_ROOT_DIR));
		Vector<String> languages;
		const String localization_dir = io::CombinePath(engine_content_root, locale::localization_directory);
		Vector<io::DirectoryEntry> localization_entries;
		if (io::EnumerateDirectoryRecursive(localization_dir, &localization_entries))
		{
			for (const io::DirectoryEntry& entry : localization_entries)
			{
				if (entry.is_file && io::GetExtension(entry.path) == locale::localization_file_extension)
				{
					languages.push_back(io::ReplaceExtension(io::GetFilename(entry.path), ""));
				}
			}
		}

		editor_locale.Initialize(engine_content_root, languages, editor_default_language);

		String selected_language = editor_settings.editor_language;
		if (selected_language.empty())
		{
			selected_language = locale::GetSystemLanguage();
		}
		if (selected_language.empty())
		{
			selected_language = editor_default_language;
		}
		editor_locale.SetLanguage(selected_language);
	}

	void EditorApplication::DrawLocalizationWindow(bool* open)
	{
		if (!open || !*open)
		{
			return;
		}
		if (!localization_editor.loaded)
		{
			ReloadLocalizationTables();
		}

		LocalizationEditorState& state = localization_editor;
		const String default_language = locale::NormalizeLanguage(loaded_project_settings.default_language);

		if (ImGui::Begin(EditorLabel(editor_key::window_project_localization, editor_window_id::project_localization), open))
		{
			if (loaded_project_settings.settings_path.empty())
			{
				ImGui::TextDisabled("%s", EditorText(editor_key::label_no_project));
				ImGui::End();
				return;
			}

			if (ImGui::Button(EditorText(editor_key::action_save)))
			{
				SaveLocalizationTables();
			}
			ImGui::SameLine();
			if (ImGui::Button(EditorText(editor_key::action_reload)))
			{
				ReloadLocalizationTables();
			}
			ImGui::SameLine();
			ImGui::TextDisabled(EditorText(editor_key::format_localization_summary),
				default_language.empty() ? "(none)" : default_language.c_str(),
				static_cast<int>(state.keys.size()),
				state.dirty ? "   *modified" : "");

			ImGui::Separator();

			ImGui::SetNextItemWidth(200.0f);
			ImGui::InputTextWithHint("##new_key", EditorText(editor_key::hint_new_key), state.new_key, sizeof(state.new_key));
			ImGui::SameLine();
			if (ImGui::Button(EditorText(editor_key::action_add_key)) && state.new_key[0] != '\0')
			{
				const String key = state.new_key;
				if (std::find(state.keys.begin(), state.keys.end(), key) == state.keys.end())
				{
					state.keys.push_back(key);
					std::sort(state.keys.begin(), state.keys.end());
					state.dirty = true;
				}
				state.new_key[0] = '\0';
			}
			ImGui::SameLine();
			ImGui::SetNextItemWidth(120.0f);
			ImGui::InputTextWithHint("##new_language", EditorText(editor_key::hint_new_language), state.new_language, sizeof(state.new_language));
			ImGui::SameLine();
			if (ImGui::Button(EditorText(editor_key::action_add_language)) && state.new_language[0] != '\0')
			{
				const String code = locale::NormalizeLanguage(state.new_language);
				if (!code.empty() && std::find(state.languages.begin(), state.languages.end(), code) == state.languages.end())
				{
					state.languages.push_back(code);
					state.tables.emplace(code, locale::Table{});
					loaded_project_settings.packaged_languages.push_back(code);
					project::SaveSettings(loaded_project_settings.settings_path, loaded_project_settings);
					state.dirty = true;
				}
				state.new_language[0] = '\0';
			}

			const int column_count = 2 + static_cast<int>(state.languages.size());
			const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable
				| ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY;
			if (column_count > 2 && ImGui::BeginTable("##localization_table", column_count, table_flags))
			{
				ImGui::TableSetupScrollFreeze(1, 1);
				ImGui::TableSetupColumn(EditorText(editor_key::label_key), ImGuiTableColumnFlags_WidthFixed, 220.0f);
				for (const String& code : state.languages)
				{
					ImGui::TableSetupColumn(code.c_str(), ImGuiTableColumnFlags_WidthStretch);
				}
				ImGui::TableSetupColumn(EditorText(editor_key::label_comment), ImGuiTableColumnFlags_WidthStretch);
				ImGui::TableHeadersRow();

				String key_to_remove;
				for (const String& key : state.keys)
				{
					ImGui::TableNextRow();
					ImGui::PushID(key.c_str());

					ImGui::TableSetColumnIndex(0);
					ImGui::TextUnformatted(key.c_str());
					if (ImGui::BeginPopupContextItem("##key_context"))
					{
						if (ImGui::MenuItem(EditorText(editor_key::action_remove_key)))
						{
							key_to_remove = key;
						}
						ImGui::EndPopup();
					}

					int column = 1;
					for (const String& code : state.languages)
					{
						ImGui::TableSetColumnIndex(column);
						locale::Table& table = state.tables[code];
						const auto found = table.find(key);
						const bool missing = found == table.end() || found->second.text.empty();
						const bool stale = !missing && code != default_language && !found->second.source.empty()
							&& found->second.source != state.tables[default_language][key].text;

						char buffer[1024] = {};
						if (found != table.end())
						{
							strncpy_s(buffer, found->second.text.c_str(), sizeof(buffer) - 1);
						}
						if (missing)
						{
							ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.45f, 0.12f, 0.12f, 0.55f)));
						}
						else if (stale)
						{
							ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImVec4(0.45f, 0.36f, 0.10f, 0.55f)));
						}
						ImGui::SetNextItemWidth(-1.0f);
						ImGui::PushID(column);
						if (ImGui::InputText("##text", buffer, sizeof(buffer)))
						{
							table[key].text = buffer;
							state.dirty = true;
						}
						if (stale && ImGui::IsItemHovered())
						{
							ImGui::SetTooltip(EditorText(editor_key::format_source_changed),
								found->second.source.c_str(), state.tables[default_language][key].text.c_str());
						}
						ImGui::PopID();
						++column;
					}

					ImGui::TableSetColumnIndex(column);
					{
						locale::Table& default_table = state.tables[default_language.empty() ? state.languages.front() : default_language];
						char comment_buffer[1024] = {};
						strncpy_s(comment_buffer, default_table[key].comment.c_str(), sizeof(comment_buffer) - 1);
						ImGui::SetNextItemWidth(-1.0f);
						if (ImGui::InputText("##comment", comment_buffer, sizeof(comment_buffer)))
						{
							for (const String& code : state.languages)
							{
								state.tables[code][key].comment = comment_buffer;
							}
							state.dirty = true;
						}
					}

					ImGui::PopID();
				}

				ImGui::EndTable();

				if (!key_to_remove.empty())
				{
					state.keys.erase(std::remove(state.keys.begin(), state.keys.end(), key_to_remove), state.keys.end());
					for (const String& code : state.languages)
					{
						state.tables[code].erase(key_to_remove);
					}
					state.dirty = true;
				}
			}
			else if (column_count <= 2)
			{
				ImGui::TextDisabled("%s", EditorText(editor_key::message_no_languages));
			}
		}
		ImGui::End();
	}

	void EditorApplication::DrawProjectSettingsWindow(bool* open)
	{
		if (!open || !*open)
		{
			return;
		}

		ImGui::SetNextWindowSize(ImVec2(520.0f, 560.0f), ImGuiCond_FirstUseEver);
		if (ImGui::Begin(EditorLabel(editor_key::window_project_settings, editor_window_id::project_settings), open))
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
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_label), ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_value), ImGuiTableColumnFlags_WidthStretch);

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
				draw_label("Clear Color");
				if (ImGui::ColorEdit3("##Clear Color", &loaded_project_settings.clear_color.r) && renderer)
				{
					renderer->SetClearColor(loaded_project_settings.clear_color);
				}

				draw_label("Anti-Aliasing");
				if (DrawEnumCombo("##Anti-Aliasing", loaded_project_settings.aa_mode))
				{
					if (editor_viewport.view)
					{
						editor_viewport.view->options.aa_mode = loaded_project_settings.aa_mode;
					}
				}

				draw_label("Tonemap Mode");
				if (DrawEnumCombo("##Tonemap Mode", loaded_project_settings.tonemap_mode))
				{
					if (editor_viewport.view)
					{
						editor_viewport.view->options.tonemap_mode = loaded_project_settings.tonemap_mode;
					}
				}

				const char* backend_items[] = { EditorText(editor_key::label_backend_directx12) };
				int backend_index = 0;
				draw_label("Backend");
				ImGui::Combo("##Backend", &backend_index, backend_items, IM_ARRAYSIZE(backend_items));

				ImGui::EndTable();
			}

			ImGui::Separator();
			if (ImGui::BeginTable("LocalizationSettingsTable", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_label), ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_value), ImGuiTableColumnFlags_WidthStretch);

				draw_label(EditorText(editor_key::label_default_language));
				draw_string_field("Default Language", loaded_project_settings.default_language);

				draw_label(EditorText(editor_key::label_packaged_languages));
				String packaged_languages;
				for (Size index = 0; index < loaded_project_settings.packaged_languages.size(); ++index)
				{
					if (index > 0)
					{
						packaged_languages += ";";
					}
					packaged_languages += loaded_project_settings.packaged_languages[index];
				}
				if (draw_string_field("Packaged Languages", packaged_languages))
				{
					loaded_project_settings.packaged_languages.clear();
					Size start = 0;
					while (start <= packaged_languages.size())
					{
						const Size separator = packaged_languages.find(';', start);
						const String item = separator == String::npos ? packaged_languages.substr(start) : packaged_languages.substr(start, separator - start);
						if (!item.empty())
						{
							loaded_project_settings.packaged_languages.push_back(item);
						}
						if (separator == String::npos)
						{
							break;
						}
						start = separator + 1;
					}
				}

				ImGui::EndTable();
			}

			ImGui::Separator();
			if (ImGui::BeginTable("SplashSettingsTable", 2, ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_label), ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_value), ImGuiTableColumnFlags_WidthStretch);

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
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_label), ImGuiTableColumnFlags_WidthFixed, 140.0f);
				ImGui::TableSetupColumn(EditorText(editor_key::label_column_value), ImGuiTableColumnFlags_WidthStretch);

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
					if (ImGui::Button(EditorLabel(editor_key::action_new, "##gdnew")))
					{
						ImGui::OpenPopup(editor_popup_id::game_data_new_schema);
					}
					if (no_project)
					{
						ImGui::EndDisabled();
					}
					if (ImGui::BeginPopup(editor_popup_id::game_data_new_schema))
					{
						ImGui::TextUnformatted(EditorText(editor_key::label_file_name));
						ImGui::SetNextItemWidth(200.0f);
						ImGui::InputText("##gd_schema_fname", game_data_editor.new_schema_filename, sizeof(game_data_editor.new_schema_filename));
						if (ImGui::Button(EditorLabel(editor_key::action_create, "##gdcreate")))
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
						if (ImGui::Button(EditorLabel(editor_key::action_cancel, "##gdcancel")))
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
					ImGui::TableSetupColumn(EditorText(editor_key::label_key), ImGuiTableColumnFlags_WidthStretch);
					ImGui::TableSetupColumn(EditorText(editor_key::label_column_type), ImGuiTableColumnFlags_WidthFixed, 70.0f);
					ImGui::TableSetupColumn(EditorText(editor_key::label_column_default), ImGuiTableColumnFlags_WidthStretch);
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
				if (ImGui::Button(EditorLabel(editor_key::action_add, "##gd_add")))
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
				if (ImGui::Button(EditorLabel(editor_key::action_save_schema, "##gd_save")))
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
			if (ImGui::Button(EditorText(editor_key::menu_save_project)))
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
		if (is_playing || path.empty() || !editor_viewport.view || !editor_viewport.view->scene)
		{
			return false;
		}

		Vector<ecs::Entity> excluded_entities;
		if (editor_viewport.view->camera_entity != ecs::INVALID_ENTITY)
		{
			excluded_entities.push_back(editor_viewport.view->camera_entity);
		}

		auto material_array = editor_viewport.view->scene->GetComponentArray<MaterialComponent>().get();
		if (material_array)
		{
			UnorderedSet<resource::Material*> saved_materials;
			for (Size material_index = 0; material_index < material_array->GetSize(); ++material_index)
			{
				MaterialComponent& material_component = material_array->data[material_index];
				if (!material_component.material || !material_component.material->IsDirty())
				{
					continue;
				}
				if (material_component.material_asset_path.empty())
				{
					backlog::Post(EditorText(editor_key::message_save_scene_failed) + String("material has no asset path"), backlog::LogLevel::Warning);
					return false;
				}
				if (!saved_materials.insert(material_component.material.get()).second)
				{
					continue;
				}

				const String material_path = project::ResolveProjectContentPath(contents_root_dir, material_component.material_asset_path);
				if (!resource::SaveMaterialBinary(material_path, material_component.material))
				{
					backlog::Post(EditorText(editor_key::message_save_scene_failed) + material_path, backlog::LogLevel::Warning);
					return false;
				}
				material_component.material->SetDirty(false);
			}
		}

		won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Write);
		won::serialize::SaveSceneDesc desc = {};
		desc.excluded_entities = &excluded_entities;
		won::serialize::SaveScene(archive, *editor_viewport.view->scene, desc);
		if (archive.HasError())
		{
			backlog::Post(EditorText(editor_key::message_save_scene_failed) + archive.GetError(), backlog::LogLevel::Warning);
			return false;
		}

		String directory = io::GetDirectoryFromPath(path);
		if (!directory.empty() && !io::CreateDirectories(directory))
		{
			backlog::Post(EditorText(editor_key::message_save_scene_failed) + directory, backlog::LogLevel::Warning);
			return false;
		}
		if (!archive.SaveToFile(path))
		{
			backlog::Post(EditorText(editor_key::message_save_scene_failed) + path, backlog::LogLevel::Warning);
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
		backlog::Post(EditorText(editor_key::message_scene_saved) + path);
		return true;
	}

	bool EditorApplication::SavePrefab(const String& path, ecs::Entity root)
	{
		if (path.empty() || root == ecs::INVALID_ENTITY || !editor_viewport.view || !editor_viewport.view->scene)
		{
			return false;
		}

		won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Write);
		if (!won::serialize::SavePrefab(archive, *editor_viewport.view->scene, root) || archive.HasError())
		{
			backlog::Post(EditorText(editor_key::message_save_prefab_failed) + archive.GetError(), backlog::LogLevel::Warning);
			return false;
		}

		String directory = io::GetDirectoryFromPath(path);
		if (!directory.empty() && !io::CreateDirectories(directory))
		{
			backlog::Post(EditorText(editor_key::message_save_prefab_failed) + directory, backlog::LogLevel::Warning);
			return false;
		}
		if (!archive.SaveToFile(path))
		{
			backlog::Post(EditorText(editor_key::message_save_prefab_failed) + path, backlog::LogLevel::Warning);
			return false;
		}

		RebuildContentBrowser();
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
					if (mesh->skeleton && mesh->skeleton->IsValid() && !mesh->animation_clips.empty())
					{
						const ecs::Entity entity = geometry_array->index_to_entity[i];
						if (ecs::AnimationComponent* animation = scene.GetComponent<ecs::AnimationComponent>(entity))
						{
							if (animation->clips.empty())
							{
								animation->clips = mesh->animation_clips;
							}
						}
					}
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
							texture_map.image = image;
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
					if (project::IsVirtualContentPath(script_slot.script_path))
					{
						script_slot.script_path = project::StripVirtualContentRoot(script_slot.script_path);
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
			backlog::Post(EditorText(editor_key::message_load_scene_failed) + path, backlog::LogLevel::Warning);
			return;
		}

		won::serialize::LoadScene(archive, *editor_viewport.view->scene);
		if (archive.HasError())
		{
			backlog::Post(EditorText(editor_key::message_load_scene_warning) + archive.GetError(), backlog::LogLevel::Warning);
		}

		current_scene_path = path;
		String relative_path = io::GetRelativePath(contents_root_dir, path);
		String config_path = relative_path.empty() ? path : relative_path;
		editor_settings.last_scene_path = config_path;
		editor_viewport.picked_entity = ecs::INVALID_ENTITY;
		editor_history.Clear();
		ResetInspectorBaseline();
		RebindSceneResources();
		CreateEditorCamera();
		UpdateEntityList();
		backlog::Post(EditorText(editor_key::message_scene_loaded) + path);
	}

	void EditorApplication::InstantiatePrefab(const String& path)
	{
		if (path.empty() || !editor_viewport.view || !editor_viewport.view->scene)
		{
			return;
		}

		won::serialize::JsonArchive archive(won::serialize::ArchiveMode::Read);
		if (!archive.LoadFromFile(path))
		{
			backlog::Post(EditorText(editor_key::message_load_prefab_failed) + path, backlog::LogLevel::Warning);
			return;
		}

		Vector<ecs::Entity> new_entities;
		const ecs::Entity root = won::serialize::LoadSceneAdditive(archive, *editor_viewport.view->scene, new_entities);
		if (root == ecs::INVALID_ENTITY)
		{
			backlog::Post(EditorText(editor_key::message_load_prefab_failed) + path, backlog::LogLevel::Warning);
			return;
		}

		RebindSceneResources();
		editor_history.PushEntityLifetime(*editor_viewport.view->scene, root, String(), EditorText(editor_key::label_instantiate_prefab_command));
		editor_viewport.picked_entity = root;
		UpdateEntityList();
		backlog::Post(EditorText(editor_key::message_prefab_added) + path);
	}

	void EditorApplication::ResetInspectorBaseline()
	{
		inspector_baseline.clear();
		inspector_baseline_entity = ecs::INVALID_ENTITY;
		inspector_item_was_active = false;
	}

	void EditorApplication::UpdateInspectorHistory()
	{
		if (is_playing || !editor_viewport.view || !editor_viewport.view->scene)
		{
			ResetInspectorBaseline();
			return;
		}

		ecs::Scene& scene = *editor_viewport.view->scene;
		const ecs::Entity picked = editor_viewport.picked_entity;
		const bool any_active = ImGui::IsAnyItemActive();
		const bool trackable = picked != ecs::INVALID_ENTITY && picked != editor_viewport.view->camera_entity && scene.IsEntityAlive(picked);

		if (!trackable || picked != inspector_baseline_entity)
		{
			ResetInspectorBaseline();
			if (trackable && !any_active)
			{
				inspector_baseline_entity = picked;
				inspector_baseline = EditorHistory::CaptureComponents(scene, picked);
			}
			inspector_item_was_active = any_active;
			return;
		}

		if (!any_active)
		{
			if (inspector_item_was_active)
			{
				editor_history.PushComponentEdit(scene, picked, std::move(inspector_baseline), EditorText(editor_key::label_edit_entity_command));
			}
			inspector_baseline = EditorHistory::CaptureComponents(scene, picked);
		}
		inspector_item_was_active = any_active;
	}

	void EditorApplication::PerformUndo()
	{
		if (is_playing || !editor_history.CanUndo() || !editor_viewport.view || !editor_viewport.view->scene)
		{
			return;
		}

		EditorContext context = {};
		context.scene = editor_viewport.view->scene;
		context.content_root = contents_root_dir;
		editor_viewport.picked_entity = editor_history.Undo(context);
		ResetInspectorBaseline();
		UpdateEntityList();
	}

	void EditorApplication::PerformRedo()
	{
		if (is_playing || !editor_history.CanRedo() || !editor_viewport.view || !editor_viewport.view->scene)
		{
			return;
		}

		EditorContext context = {};
		context.scene = editor_viewport.view->scene;
		context.content_root = contents_root_dir;
		editor_viewport.picked_entity = editor_history.Redo(context);
		ResetInspectorBaseline();
		UpdateEntityList();
	}

	void EditorApplication::EnterPlay()
	{
		if (is_playing || !edit_scene || !editor_viewport.view)
		{
			return;
		}

		Vector<ecs::Entity> excluded_entities;
		if (editor_viewport.view->camera_entity != ecs::INVALID_ENTITY)
		{
			excluded_entities.push_back(editor_viewport.view->camera_entity);
		}

		won::serialize::JsonArchive write_archive(won::serialize::ArchiveMode::Write);
		won::serialize::SaveSceneDesc save_desc = {};
		save_desc.excluded_entities = &excluded_entities;
		won::serialize::SaveScene(write_archive, *edit_scene, save_desc);
		String scene_data;
		if (!write_archive.SaveToString(scene_data))
		{
			return;
		}

		ecs::SceneDesc play_desc = {};
		play_desc.script_runtime = script_runtime.get();
		play_desc.physics = project::GetPhysicsDesc(loaded_project_settings);
		play_desc.audio_mixer = audio_mixer.get();
		play_desc.enable_simulation = true;
		play_scene = &GetSceneManager()->CreateScene(play_desc);

		for (const EditorPluginInfo& plugin_info : plugins)
		{
			if (plugin_info.plugin)
			{
				RegisterPluginExtensions(plugin_info.plugin, *play_scene);
			}
		}

		won::serialize::JsonArchive read_archive(won::serialize::ArchiveMode::Read);
		read_archive.LoadFromString(scene_data);
		won::serialize::LoadScene(read_archive, *play_scene);
		resource::LoadSceneResources(*play_scene, contents_root_dir);
		if (device)
		{
			rendering::utils::FlushEnqueuedResourceUploads(*device);
		}

		edit_camera_entity = editor_viewport.view->camera_entity;
		editor_viewport.view->scene = play_scene;
		editor_viewport.view->camera_entity = ecs::INVALID_ENTITY;
		editor_viewport.view->manual_camera = false;

		editor_viewport.picked_entity = ecs::INVALID_ENTITY;
		is_paused = false;
		is_playing = true;
		UpdateEntityList();
		backlog::Post("[PlayMode] entered, play scene entities: " + std::to_string(play_scene->GetEntities().size()));
	}

	void EditorApplication::ExitPlay()
	{
		if (!is_playing)
		{
			return;
		}

		WaitIdle();
		if (editor_viewport.view)
		{
			editor_viewport.view->scene = edit_scene;
			editor_viewport.view->camera_entity = edit_camera_entity;
			editor_viewport.view->manual_camera = true;
		}
		GetSceneManager()->DestroyScene(play_scene);
		play_scene = nullptr;
		editor_viewport.picked_entity = ecs::INVALID_ENTITY;
		is_paused = false;
		is_playing = false;
		UpdateEntityList();
		backlog::Post("[PlayMode] exited, edit scene entities: " + std::to_string(edit_scene ? edit_scene->GetEntities().size() : 0));
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
			if (!is_playing && (entity == editor_viewport.view->camera_entity))
			{
				continue;
			}
			sorted_entities.push_back(entity);
		}

		std::sort(sorted_entities.begin(), sorted_entities.end());
	}

}
