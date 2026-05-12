#include "Editor.h"
#include "Input.h"
#include "ShaderCompiler.h"
#include "RHIResource.h"
#include "RHIShader.h"
#include "RHIPipeline.h"
#include "RenderingUtils.h"
#include "ShaderCompiler.h"
#include "FileSystem.h"
#include "Image.h"
#include "StringUtils.h"
#include "Backlog.h"
#include "Profiler.h"
#include "SceneComponents.h"
#include "JobSystem.h"
#include "EventHandler.h"

#include "AssetImporter/AssetImporter.h"
#include "CameraController/CameraController.h"
#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui-docking/imgui.h"
#include "imgui-docking/imgui_internal.h"
#ifdef _WIN32
#include "imgui-docking/imgui_impl_win32.h"
#endif
#include "IconsMaterialDesign.h"
#include "Themes.h"

#define DEFAULTBUTTONWIDTH 200

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace won::editor
{
	using namespace resource;
	using namespace rendering;
	using namespace plugin;
	using namespace ecs;

	static RHIShader imgui_vs;
	static RHIShader imgui_ps;
	static RHIShader editor_grid_vs;
	static RHIShader editor_grid_ps;
	static String contents_root_dir = String(CONTENTS_ROOT_DIR) + "/";
	namespace
	{
		float3 QuaternionToEulerXYZDegrees(const float4& quaternion)
		{
			XMVECTOR xquaternion = XMVector4Normalize(XMLoadFloat4(&quaternion));
			XMMATRIX rotation_matrix = XMMatrixRotationQuaternion(xquaternion);
			float4x4 matrix = {};
			XMStoreFloat4x4(&matrix, rotation_matrix);

			float pitch = asin(-matrix._32);
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

		bool DrawComponentRemoveButton(const char* component_name, bool can_remove = true)
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
				ImGui::SetTooltip("Remove %s", component_name);
			}

			if (pressed && can_remove)
			{
				ImGui::OpenPopup("Remove Component");
			}

			bool remove_component = false;
			if (ImGui::BeginPopup("Remove Component", ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::Text("Remove %s?", component_name);
				ImGui::TextDisabled("This action cannot be undone.");

				if (ImGui::Button("Remove"))
				{
					remove_component = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::SameLine();
				if (ImGui::Button("Cancel"))
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
				std::string font_icon_path = std::string(CONTENTS_ROOT_DIR) + "/Fonts/MaterialIcons-Regular.ttf";
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

			ImGui::DockBuilderDockWindow("Viewport", dock_main);
			ImGui::DockBuilderDockWindow("Inspector", dock_right);
			ImGui::DockBuilderDockWindow("Contents Browser", dock_bottom);
			ImGui::DockBuilderDockWindow("Log", dock_bottom);
			ImGui::DockBuilderDockWindow("Profiler", dock_bottom);
			//ImGui::DockBuilderDockWindow("Scene Tree", dock_left);
			ImGui::DockBuilderDockWindow("Entity List", dock_left);

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

	void EditorApplication::Initialize(const ApplicationDesc& desc)
	{
		Application::Initialize(desc);

		{
			ShaderCompilerOptions compiler_options;
			compiler_options.backend = ShaderCompilerBackend::DXC;
			compiler_options.shader_source_root_path = contents_root_dir + "CustomShaders";
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

		std::string font_folder_path = contents_root_dir + "Fonts";
		AddImGuiFont(font_folder_path + "/Noto_Sans_KR/static", "NotoSansKR-Regular.ttf");

		InitImGui();
		InitEditorGrid();

		plugin_manager = std::make_shared<plugin::PluginManager>();
		LoadDefaultPlugins();

		// camera entity
		{
			camera_entity = scene.CreateEntity();
			auto camera_transform = scene.AddComponent<ecs::TransformComponent>(camera_entity);
			if (camera_transform)
			{
				camera_transform->position = { -4.7f, 2.0f, 0.3f };
				camera_transform->RotateRollPitchYaw({ 0.f, math::PI / 2.f, 0} );
				camera_transform->SetDirty();
			}

			auto camera = scene.AddComponent<ecs::CameraComponent>(camera_entity);
			if (camera)
			{
				float viewport_width = static_cast<float>(main_view.viewport.width);
				float viewport_height = static_cast<float>(main_view.viewport.height);
				if (viewport_height <= 0.0f)
				{
					viewport_height = 1.0f;
				}
				camera->SetAspectRatio(viewport_width / viewport_height);
				camera->SetNearFar(0.1f, 1000.0f);
				camera->SetFOV_Y(math::PI / 3.0f);
				camera->SetOrtho(false);
				//camera->SetOrthoVerticalSize(4.f);
			}

			auto name = scene.AddComponent<ecs::NameComponent>(camera_entity);
			name->value = "Editor Camera";
		}
		main_view.scene = &scene; // empty scene
		main_view.camera_entity = camera_entity;

		//main_viewport_pos = { 0, 0 };
		//main_viewport_size = { static_cast<float>(main_view.viewport.width), static_cast<float>(main_view.viewport.height) };
		LoadSampleScene();
	}

	void EditorApplication::Shutdown()
	{
		imgui_pso.reset();
		editor_grid_pso.reset();
		imgui_font.reset();
		imgui_font_subresource = {};
		imgui_sampler.reset();
		editor_primitive_mesh.reset();
		deferred_primitive_removal_buffers.clear();
		deferred_entity_removal_resources.clear();
		asset_import_tasks.clear();
		editor_primitive_entity = ecs::INVALID_ENTITY;
		scene = {};

		plugin_manager = {};

		Application::Shutdown();
	}

	void EditorApplication::Update(float dt)
	{
		Application::Update(dt);
		for (auto it = deferred_entity_removal_resources.begin(); it != deferred_entity_removal_resources.end();)
		{
			if (it->frames_left > 0)
			{
				--it->frames_left;
			}
			if (it->frames_left == 0)
			{
				it = deferred_entity_removal_resources.erase(it);
			}
			else
			{
				++it;
			}
		}

		bool entity_list_dirty = false;
		for (auto it = asset_import_tasks.begin(); it != asset_import_tasks.end();)
		{
			std::shared_ptr<AssetImportTask> task = *it;
			if (!task)
			{
				it = asset_import_tasks.erase(it);
				continue;
			}

			if (task->committed.load())
			{
				ecs::Entity root_entity = task->root_entity.load();
				auto transform = scene.GetComponent<ecs::TransformComponent>(root_entity);
				if (transform)
				{
					//transform->Translate({ 5.0f, 0.0f, 5.0f });
					//transform->Scale({ 3.0f, 3.0f, 3.0f });
				}

				auto material_component = scene.GetComponent<ecs::MaterialComponent>(root_entity);
				if (material_component)
				{
					for (uint32 i = 0; i < (uint32)material_component->GetMaterialSlotCount(); i++)
					{
						//auto& slot = material_component->GetMaterialSlot(i);
						//slot.shader_type =
					}
				}

				auto geometry_component = scene.GetComponent<ecs::GeometryComponent>(root_entity);
				if (geometry_component)
				{
					geometry_component->SetCastShadow(true);
				}

				entity_list_dirty = true;
				it = asset_import_tasks.erase(it);
				continue;
			}
			if (task->failed.load() || task->finished.load())
			{
				it = asset_import_tasks.erase(it);
				continue;
			}
			++it;
		}
		if (entity_list_dirty)
		{
			UpdateEntityList();
		}

		if (renderer)
		{
			rendering::RendererDebugOptions debug_options = {};
			debug_options.ddgi_debug_enable = viewport_debug_settings.show_ddgi_overlay;
			debug_options.bvh_debug_enable = viewport_debug_settings.show_bvh_debug;
			debug_options.wireframe_enable = viewport_debug_settings.use_wireframe;
			renderer->SetDebugOptions(debug_options);
		}
		UpdateEditorPrimitiveMesh();

		if (won::io::IsPressed(io::Button('R')))
		{
			rendering::ReloadShaderLibrary(device);
		}

		static CameraControllerAPI* controller_api = nullptr;
		static IPlugin* camera_controller = nullptr;
		if (!controller_api)
		{
			camera_controller = plugin_manager->GetPlugin(WON_IID_CAMERA_CONTROLLER).get();
			controller_api = (CameraControllerAPI*)camera_controller->QueryInterface(WON_IID_CAMERA_CONTROLLER, WON_VID_CAMERA_CONTROLLER);
		}

		auto camera = scene.GetComponent<ecs::CameraComponent>(camera_entity);
		auto transform = scene.GetComponent<ecs::TransformComponent>(camera_entity);
		if (!camera || !transform)
		{
			return;
		}

		// assume editor camera has no hierarchy
		XMVECTOR xforward = XMVector3Normalize(XMLoadFloat3(&camera->forward));
		XMVECTOR xup = XMVector3Normalize(XMLoadFloat3(&camera->up));
		XMVECTOR xright = XMVector3Normalize(XMVector3Cross(xup, xforward));

		const float camera_speed = 5.0f;

		if (won::io::IsDown(io::Button('W')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xforward, dt * camera_speed));
			transform->Translate(translation);
		}
		if (won::io::IsDown(io::Button('A')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xright, -dt * camera_speed));
			transform->Translate(translation);
		}
		if (won::io::IsDown(io::Button('S')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xforward, -dt * camera_speed));
			transform->Translate(translation);
		}
		if (won::io::IsDown(io::Button('D')))
		{
			float3 translation{};
			XMStoreFloat3(&translation, XMVectorScale(xright, dt * camera_speed));
			transform->Translate(translation);
		}

		float2 mouse_pos = io::GetMouseState().position;
		float2 main_viewport_pos = { (float)main_view.viewport.x, (float)main_view.viewport.y};
		float2 main_viewport_size = { (float)main_view.viewport.width, (float)main_view.viewport.height};
		float2 viewport_mouse_pos = { mouse_pos.x - main_viewport_pos.x, mouse_pos.y - main_viewport_pos.y };

		static bool pressed = false;
		static float2 prev_mouse_pos{};
		static CameraInteractionMode active_interaction = CameraInteractionMode::None;

		if (viewport_input_enabled &&
			0 <= viewport_mouse_pos.x && viewport_mouse_pos.x <= main_viewport_size.x &&
			0 <= viewport_mouse_pos.y && viewport_mouse_pos.y <= main_viewport_size.y)
		{
			if (io::IsPressed(io::Button::MOUSE_BUTTON_LEFT))
			{
				ecs::RayCastHit hit = {};
				if (main_view.RayCast(mouse_pos, hit, true))
				{
					picked_entity = hit.entity;
				}
				else
				{
					picked_entity = ecs::INVALID_ENTITY;
				}
			}

			if (io::IsPressed(io::Button::MOUSE_BUTTON_RIGHT) || io::IsPressed(io::Button::MOUSE_BUTTON_MIDDLE))
			{
				pressed = true;

				ControllerState controller_state;
				controller_state.rotate_speed = 1.f;
				controller_state.screen_size = main_viewport_size;
				controller_state.zoom_speed = 0.005f;
				controller_state.rotate_speed = 1.f;
				controller_state.orbit_speed = 1.f;
				controller_state.focus_point = { 0.f, 0.f, 0.f };

				controller_api->SetControllerState(camera_controller, controller_state);

				CameraState camera_state;
				camera_state.cam_pos = transform->position;
				camera_state.cam_rotation = transform->rotation;

				if (io::IsPressed(io::Button::MOUSE_BUTTON_RIGHT))
				{
					active_interaction = CameraInteractionMode::Rotate;
				}
				else if (io::IsPressed(io::Button::MOUSE_BUTTON_MIDDLE))
				{
					active_interaction = CameraInteractionMode::Orbit;
				}
				// PanMove is temporarily disabled so left mouse can be used for scene picking.
				// else
				// {
				// 	active_interaction = CameraInteractionMode::PanMove;
				// }

				controller_api->BeginInteraction(camera_controller, active_interaction, camera_state);

				prev_mouse_pos = viewport_mouse_pos;
			}
		}

		if (pressed)
		{
			float2 mouse_delta = { viewport_mouse_pos.x - prev_mouse_pos.x, viewport_mouse_pos.y - prev_mouse_pos.y };
			if (!math::float_equal(mouse_delta.x, 0.f)
				|| !math::float_equal(mouse_delta.y, 0.f))
			{
				// assume editor camera has no hierarchy
				transform->UpdateTransform();
				XMMATRIX xmat = transform->GetWorldTransform();
				float4x4 mat{};
				XMStoreFloat4x4(&mat, xmat);
				float3 cam_pos = math::GetPosition(mat);

				CameraState camera_state;
				camera_state.cam_pos = cam_pos;
				camera_state.cam_rotation = transform->rotation;

				// PanMove is temporarily disabled so left mouse can be used for scene picking.
				// const bool is_panmove = io::IsDown(io::Button::MOUSE_BUTTON_LEFT);
				const bool is_panmove = false;
				const bool is_rotate = io::IsDown(io::Button::MOUSE_BUTTON_RIGHT);
				const bool is_orbit = io::IsDown(io::Button::MOUSE_BUTTON_MIDDLE);

				if (is_panmove || is_rotate || is_orbit)
				{
					controller_api->UpdateInteraction(camera_controller, mouse_delta, camera_state);
					transform->position = camera_state.cam_pos;
					transform->rotation = camera_state.cam_rotation;
					transform->SetDirty();
				}

				prev_mouse_pos = viewport_mouse_pos;
			}

			bool interaction_finished = false;
			if (active_interaction == CameraInteractionMode::PanMove)
			{
				// PanMove is temporarily disabled so left mouse can be used for scene picking.
				// interaction_finished = !io::IsDown(io::Button::MOUSE_BUTTON_LEFT);
				interaction_finished = true;
			}
			else if (active_interaction == CameraInteractionMode::Rotate)
			{
				interaction_finished = !io::IsDown(io::Button::MOUSE_BUTTON_RIGHT);
			}
			else if (active_interaction == CameraInteractionMode::Orbit)
			{
				interaction_finished = !io::IsDown(io::Button::MOUSE_BUTTON_MIDDLE);
			}

			if (interaction_finished)
			{
				controller_api->EndInteraction(camera_controller);
				pressed = false;
				active_interaction = CameraInteractionMode::None;
			}
		}
		
	}

	void EditorApplication::LoadDefaultPlugins()
	{
		if (!plugin_manager->LoadPlugin(WON_IID_ASSET_IMPORTER))
		{

		}
		if (!plugin_manager->LoadPlugin(WON_IID_CAMERA_CONTROLLER))
		{

		}
	}

	void EditorApplication::UpdateEditorPrimitiveMesh()
	{
		if (editor_primitive_entity == ecs::INVALID_ENTITY || !editor_primitive_mesh)
		{
			editor_primitive_entity = scene.CreateEntity();
			editor_primitive_mesh = std::make_shared<resource::Mesh>();

			scene.AddComponent<ecs::TransformComponent>(editor_primitive_entity);

			if (auto geometry = scene.AddComponent<ecs::GeometryComponent>(editor_primitive_entity))
			{
				geometry->SetMesh(editor_primitive_mesh);
				geometry->SetExcludeFromBVH(true);
			}

			scene.AddComponent<ecs::MaterialComponent>(editor_primitive_entity);

			if (auto name = scene.AddComponent<ecs::NameComponent>(editor_primitive_entity))
			{
				name->value = "Editor Primitives";
			}
		}

		if (!editor_primitive_mesh)
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
			theme::gpu_bvh_leaf_color
		};

		if (auto material = scene.GetComponent<ecs::MaterialComponent>(editor_primitive_entity))
		{
			material->material_slots.clear();
			for (const float4& color : primitive_material_colors)
			{
				auto& material_slot = material->AddMaterialSlot();
				material_slot.base_color = color;
				material_slot.metallic = 0.0f;
				material_slot.roughness = 1.0f;
			}
		}

		editor_primitive_mesh->positions.clear();
		editor_primitive_mesh->indices.clear();
		editor_primitive_mesh->submeshes.clear();

		struct PrimitiveBatch
		{
			uint32 first_index = 0;
			uint32 index_count = 0;
			uint32 material_slot = 0;
		};

		Vector<PrimitiveBatch> primitive_batches;

		auto add_line = [&](const float3& from, const float3& to, uint32 material_slot)
		{
			const uint32 first_vertex = static_cast<uint32>(editor_primitive_mesh->positions.size());
			const uint32 first_index = static_cast<uint32>(editor_primitive_mesh->indices.size());
			editor_primitive_mesh->positions.push_back(from);
			editor_primitive_mesh->positions.push_back(to);
			editor_primitive_mesh->indices.push_back(first_vertex);
			editor_primitive_mesh->indices.push_back(first_vertex + 1);
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

		if (viewport_debug_settings.show_ddgi_overlay && renderer)
		{
			const rendering::RendererDebugDDGIState& ddgi_state = renderer->GetDebugState().ddgi;
			if (ddgi_state.volume_active)
			{
				if (viewport_debug_settings.show_ddgi_volume)
				{
					add_box(ddgi_state.volume_min, ddgi_state.volume_max, EditorPrimitiveDDGIVolume);
				}

				if (viewport_debug_settings.show_ddgi_probes && ddgi_state.total_probe_count > 0)
				{
					const float min_probe_spacing = (std::min)(ddgi_state.probe_spacing.x, (std::min)(ddgi_state.probe_spacing.y, ddgi_state.probe_spacing.z));
					const float point_size = (std::max)(0.04f, min_probe_spacing * 0.08f);
					uint32 drawn_probe_count = 0;
					const uint32 max_probe_draw_count = static_cast<uint32>((std::max)(1, viewport_debug_settings.ddgi_max_probe_draw_count));
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

		if (viewport_debug_settings.show_bvh_debug && renderer)
		{
			const rendering::RendererDebugBVHState& bvh_state = renderer->GetDebugState().bvh;
			if (viewport_debug_settings.show_cpu_bvh_nodes && bvh_state.cpu_bvh_available)
			{
				for (const rendering::RendererDebugBVHState::BVHNode& node : bvh_state.cpu_nodes)
				{
					add_box(node.bounds_min, node.bounds_max, node.is_leaf ? EditorPrimitiveCPUBVHLeaf : EditorPrimitiveCPUBVHInternal);
				}
			}
			if (viewport_debug_settings.show_gpu_bvh_nodes && bvh_state.gpu_bvh_available)
			{
				for (const rendering::RendererDebugBVHState::BVHNode& node : bvh_state.gpu_nodes)
				{
					add_box(node.bounds_min, node.bounds_max, node.is_leaf ? EditorPrimitiveGPUBVHLeaf : EditorPrimitiveGPUBVHInternal);
				}
			}
		}

		if (editor_primitive_mesh->render_data.IsValid())
		{
			if (editor_primitive_mesh->render_data.buffer)
			{
				deferred_primitive_removal_buffers.push_back(editor_primitive_mesh->render_data.buffer);
				constexpr Size max_retired_primitive_buffers = 8;
				if (deferred_primitive_removal_buffers.size() > max_retired_primitive_buffers)
				{
					deferred_primitive_removal_buffers.erase(deferred_primitive_removal_buffers.begin());
				}
			}
		}

		editor_primitive_mesh->ClearRenderData();
		if (!editor_primitive_mesh->indices.empty())
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
					const uint32 vertex_index = editor_primitive_mesh->indices[index];
					if (vertex_index >= editor_primitive_mesh->positions.size())
					{
						continue;
					}
					math::AABB vertex_bounds = {};
					vertex_bounds.min = editor_primitive_mesh->positions[vertex_index];
					vertex_bounds.max = editor_primitive_mesh->positions[vertex_index];
					submesh.local_bounds.Merge(vertex_bounds);
				}
				editor_primitive_mesh->submeshes.push_back(submesh);
			};

			for (const PrimitiveBatch& batch : primitive_batches)
			{
				if (batch.index_count > 0)
				{
					make_submesh(batch.first_index, batch.index_count, resource::PrimitiveTopology::LineList, batch.material_slot);
				}
			}

			rendering::utils::CreateRenderData(*device, *editor_primitive_mesh);
		}

		if (auto geometry = scene.GetComponent<ecs::GeometryComponent>(editor_primitive_entity))
		{
			geometry->UpdateLocalBounds();
			geometry->SetDirty();
		}
	}

	void EditorApplication::OnWindowResized(int width, int height)
	{
		auto* camera = scene.GetComponent<ecs::CameraComponent>(camera_entity);
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
			if (ext == "png" || ext == "jpg" || ext == "jpeg" || ext == "dds" || ext == "tga" || ext == "bmp") return ContentAssetType::Texture;
			if (ext == "mat") return ContentAssetType::Material;
			if (ext == "fbx" || ext == "obj" || ext == "gltf" || ext == "glb") return ContentAssetType::Mesh;
			if (ext == "scene" || ext == "json") return ContentAssetType::Scene;
			if (ext == "hlsl" || ext == "hlsli") return ContentAssetType::Shader;
			if (ext == "ttf" || ext == "otf") return ContentAssetType::Font;
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

			ContentBrowserAsset asset = {};
			asset.disk_path = entry.path;
			asset.virtual_path = virtual_path;
			asset.name = io::GetFilename(entry.path);
			String extension = io::GetExtension(entry.path);
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
			case ContentAssetType::Texture: return "Texture";
			case ContentAssetType::Material: return "Material";
			case ContentAssetType::Mesh: return "Mesh";
			case ContentAssetType::Scene: return "Scene";
			case ContentAssetType::Shader: return "Shader";
			case ContentAssetType::Font: return "Font";
			case ContentAssetType::Unknown: return "Unknown";
			default: return "Asset";
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
			default: return ICON_MD_INSERT_DRIVE_FILE;
			}
		};

		ImGui::PushID(asset.virtual_path.c_str());
		ImGui::BeginGroup();
		const bool can_import_asset = asset.type == ContentAssetType::Mesh;
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
		if (ImGui::BeginPopupContextItem("ContentAssetContext"))
		{
			if (ImGui::MenuItem("Import to Scene", nullptr, false, can_import_asset))
			{
				content_browser.pending_import_name = asset.name;
				content_browser.pending_import_virtual_path = asset.virtual_path;
				content_browser.pending_import_disk_path = asset.disk_path;
				content_browser.pending_import_type = asset.type;
				content_browser.open_import_confirm = true;
			}
			ImGui::Separator();
			if (ImGui::MenuItem("Copy Disk Path"))
			{
				ImGui::SetClipboardText(asset.disk_path.c_str());
			}
			if (ImGui::MenuItem("Copy Virtual Path"))
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
			case ContentAssetType::All: return "All Types";
			case ContentAssetType::Texture: return "Texture";
			case ContentAssetType::Material: return "Material";
			case ContentAssetType::Mesh: return "Mesh";
			case ContentAssetType::Scene: return "Scene";
			case ContentAssetType::Shader: return "Shader";
			case ContentAssetType::Font: return "Font";
			case ContentAssetType::Unknown: return "Unknown";
			default: return "All Types";
			}
		};

		ImGui::TextUnformatted("Path:");
		ImGui::SameLine();
		String current_folder = content_browser.current_folder;
		Vector<String> breadcrumb_parts;
		breadcrumb_parts.push_back("Contents");
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
		ImGui::InputTextWithHint("##content_search", ICON_MD_SEARCH " Search", content_browser.search, arraysize(content_browser.search));

		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		if (ImGui::BeginCombo("##content_type_filter", type_name(content_browser.type_filter)))
		{
			const ContentAssetType filters[] = { ContentAssetType::All, ContentAssetType::Texture, ContentAssetType::Material, ContentAssetType::Mesh, ContentAssetType::Scene, ContentAssetType::Shader, ContentAssetType::Font, ContentAssetType::Unknown };
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
		ImGui::SliderFloat("##content_tile_size", &content_browser.tile_size, 48.0f, 128.0f, "Size %.0f");

		ImGui::SameLine();
		if (ImGui::Button(ICON_MD_REFRESH))
		{
			RebuildContentBrowser();
		}
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("Refresh");
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
				ImGui::TextDisabled("Folder");
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
			ImGui::TextDisabled("Folder is empty.");
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
			ImGui::OpenPopup("Import Content Asset");
			content_browser.open_import_confirm = false;
		}

		if (ImGui::BeginPopup("Import Content Asset", ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextUnformatted("Import this asset to Scene?");
			ImGui::TextUnformatted(content_browser.pending_import_name.c_str());
			ImGui::TextDisabled("%s", content_browser.pending_import_virtual_path.c_str());
			const bool can_import = content_browser.pending_import_type == ContentAssetType::Mesh && !content_browser.pending_import_disk_path.empty();
			if (!can_import)
			{
				ImGui::BeginDisabled();
			}
			if (ImGui::Button("Import"))
			{
				auto asset_importer = plugin_manager->GetPlugin(WON_IID_ASSET_IMPORTER);
				AssetImporterAPI* api = asset_importer ? (AssetImporterAPI*)asset_importer->QueryInterface(WON_IID_ASSET_IMPORTER, WON_VID_ASSET_IMPORTER) : nullptr;
				std::shared_ptr<AssetImportTask> import_task;
				if (api)
				{
					import_task = api->ImportAsync(asset_importer.get(), content_browser.pending_import_disk_path.c_str(), &scene, device.get());
				}
				if (import_task)
				{
					asset_import_tasks.push_back(import_task);
				}
				else
				{
					backlog::Post("Content Browser import failed: " + content_browser.pending_import_disk_path, backlog::LogLevel::Warning);
				}
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
			if (ImGui::Button("Cancel"))
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
		if (!viewport_debug_settings.show_grid || !editor_grid_pso || !renderer)
		{
			return;
		}

		const ecs::CameraComponent* camera = scene.GetComponent<ecs::CameraComponent>(camera_entity);
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
			viewport.x = static_cast<float>(main_view.viewport.x);
			viewport.y = static_cast<float>(main_view.viewport.y);
			viewport.width = static_cast<float>(main_view.viewport.width);
			viewport.height = static_cast<float>(main_view.viewport.height);
			viewport.min_depth = 0.0f;
			viewport.max_depth = 1.0f;

			RHIRect scissor = {};
			scissor.x = main_view.scissor.x;
			scissor.y = main_view.scissor.y;
			scissor.width = main_view.scissor.width;
			scissor.height = main_view.scissor.height;

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
		ImGui::Begin("Main", NULL, flags);

		ImGui::PopStyleVar(3);

		ImGuiID dockspace_id = ImGui::GetID("MainDockSpace");

		if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
			BuildDefaultDockLayout(dockspace_id, io.DisplaySize);

		if (ImGui::BeginMenuBar())
		{
			if (ImGui::BeginMenu("Window"))
			{
				if (ImGui::MenuItem("Reset Layout"))
					BuildDefaultDockLayout(dockspace_id, io.DisplaySize);
				ImGui::EndMenu();
			}

			ImGui::EndMenuBar();
		}

		ImGui::DockSpace(dockspace_id, ImVec2(0, 0), ImGuiDockNodeFlags_PassthruCentralNode);

		ImGui::End();

		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		viewport_input_enabled = false;
		if (ImGui::Begin("Viewport"))
		{
			if (ImGui::Button("Options"))
			{
				ImGui::OpenPopup("OptionsPopup");
				//ImGui::SetNextWindowSizeConstraints(ImVec2(240, 160), ImVec2(600, 500));
			}

			ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, ImVec2(500, 500));
			if (ImGui::BeginPopup("OptionsPopup"))
			{
				ImGui::Checkbox("WireFrame", &viewport_debug_settings.use_wireframe);

				if (window)
				{
					std::shared_ptr<RHISwapchain> swapchain = window->GetRHISwapchain();
					if (swapchain)
					{
						bool vsync_enabled = swapchain->IsVSyncEnabled();
						if (ImGui::Checkbox("VSync", &vsync_enabled))
						{
							swapchain->SetVSync(vsync_enabled);
						}
					}
				}

				ImGui::Separator();
				ImGui::Checkbox("Editor Grid", &viewport_debug_settings.show_grid);
				ImGui::Separator();
				ImGui::Checkbox("BVH Debug", &viewport_debug_settings.show_bvh_debug);
				if (viewport_debug_settings.show_bvh_debug)
				{
					ImGui::Checkbox("CPU BVH Nodes", &viewport_debug_settings.show_cpu_bvh_nodes);
					viewport_debug_settings.show_gpu_bvh_nodes = false;
					ImGui::BeginDisabled();
					ImGui::Checkbox("GPU BVH Nodes", &viewport_debug_settings.show_gpu_bvh_nodes);
					ImGui::EndDisabled();
				}

				ImGui::Separator();
				ImGui::Checkbox("DDGI Debug Overlay", &viewport_debug_settings.show_ddgi_overlay);
				if (viewport_debug_settings.show_ddgi_overlay)
				{
					ImGui::Checkbox("DDGI Volume", &viewport_debug_settings.show_ddgi_volume);
					ImGui::Checkbox("DDGI Probes", &viewport_debug_settings.show_ddgi_probes);
					ImGui::Checkbox("DDGI Text", &viewport_debug_settings.show_ddgi_text);
					ImGui::InputInt("DDGI Max Probe Draw", &viewport_debug_settings.ddgi_max_probe_draw_count);
					viewport_debug_settings.ddgi_max_probe_draw_count = (std::max)(1, viewport_debug_settings.ddgi_max_probe_draw_count);
				}

				ImGui::Separator();
				if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();

				ImGui::EndPopup();
			}
			ImGui::PopStyleVar();

			ImVec2 viewport_region_min = ImGui::GetWindowContentRegionMin();
			ImVec2 viewport_region_max = ImGui::GetWindowContentRegionMax();
			ImVec2 window_pos = ImGui::GetWindowPos();
			ImVec2 viewport_pos = ImVec2(window_pos.x + viewport_region_min.x, window_pos.y + viewport_region_min.y);
			ImVec2 viewport_size = ImVec2(viewport_region_max.x - viewport_region_min.x, viewport_region_max.y - viewport_region_min.y);
			viewport_input_enabled = ImGui::IsWindowHovered() && !ImGui::IsAnyItemHovered() && !ImGui::IsAnyItemActive();

			main_view.viewport.x = static_cast<uint32>(viewport_pos.x);
			main_view.viewport.y = static_cast<uint32>(viewport_pos.y);
			main_view.viewport.width = (std::max)(1u, static_cast<uint32>(viewport_size.x));
			main_view.viewport.height = (std::max)(1u, static_cast<uint32>(viewport_size.y));
			main_view.scissor.x = main_view.viewport.x;
			main_view.scissor.y = main_view.viewport.y;
			main_view.scissor.width = main_view.viewport.width;
			main_view.scissor.height = main_view.viewport.height;

			if (auto* camera = scene.GetComponent<ecs::CameraComponent>(camera_entity))
			{
				camera->SetAspectRatio(static_cast<float>(main_view.viewport.width) / static_cast<float>(main_view.viewport.height));
				if (viewport_debug_settings.show_ddgi_overlay)
				{
					rendering::RendererDebugState renderer_debug_state = {};
					if (renderer)
					{
						renderer_debug_state = renderer->GetDebugState();
					}

					DrawDDGIDebugOverlay(
						*camera,
						renderer_debug_state,
						viewport_pos,
						viewport_size,
						viewport_debug_settings.show_ddgi_volume,
						viewport_debug_settings.show_ddgi_probes,
						viewport_debug_settings.show_ddgi_text,
						viewport_debug_settings.ddgi_max_probe_draw_count);
				}
			}
		}
		ImGui::End();
		ImGui::PopStyleVar();
		ImGui::PopStyleColor();

		//ImGui::Begin("Scene Tree");
		//ImGui::Text("...");
		//ImGui::End();

		if (ImGui::Begin("Entity List"))
		{
			static int selected_index = -1;
			static ecs::Entity pending_delete_entity = INVALID_ENTITY;
			static ImVec2 delete_entity_popup_pos = {};
			ecs::Entity delete_entity = INVALID_ENTITY;
			bool open_delete_entity_confirm = false;

			Size running_import_count = 0;
			for (const std::shared_ptr<AssetImportTask>& task : asset_import_tasks)
			{
				if (task && !task->finished.load())
				{
					++running_import_count;
				}
			}
			if (running_import_count > 0)
			{
				const int dot_count = static_cast<int>(ImGui::GetTime() * 3.0) % 4;
				std::string import_status = running_import_count == 1 ? "Importing asset" : "Importing assets (" + std::to_string(running_import_count) + ")";
				import_status.append(dot_count, '.');
				ImGui::TextDisabled("%s", import_status.c_str());
				ImGui::Separator();
			}

			if (ImGui::Button("+"))
			{
				ecs::Entity entity = scene.CreateEntity();
				auto transform = scene.AddComponent<TransformComponent>(entity);
				auto name = scene.AddComponent<NameComponent>(entity);
				UpdateEntityList();
				picked_entity = entity;

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

						std::string label = "Entity " + std::to_string(id);
						auto name = main_view.scene->GetComponent<ecs::NameComponent>(id);
						if (name != nullptr)
						{
							label += " (" + name->value + ")";
						}

						const bool is_selected = (picked_entity == id);
						if (ImGui::Selectable(label.c_str(), is_selected))
						{
							selected_index = i;
							picked_entity = id;
						}

						if (ImGui::BeginPopupContextItem("EntityContextMenu"))
						{
							selected_index = i;
							picked_entity = id;

							if (ImGui::MenuItem("Delete Entity", nullptr, false, id != camera_entity))
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
				ImGui::OpenPopup("Delete Entity Confirm");
			}

			ImGui::SetNextWindowPos(delete_entity_popup_pos, ImGuiCond_Appearing);
			if (ImGui::BeginPopup("Delete Entity Confirm", ImGuiWindowFlags_AlwaysAutoResize))
			{
				String entity_label = "Entity " + std::to_string(pending_delete_entity);
				if (ecs::NameComponent* name = main_view.scene->GetComponent<ecs::NameComponent>(pending_delete_entity))
				{
					entity_label += " (" + name->value + ")";
				}

				ImGui::Text("Delete %s?", entity_label.c_str());
				ImGui::TextDisabled("Children and components will also be removed.");

				const bool can_delete_entity = pending_delete_entity != INVALID_ENTITY && pending_delete_entity != camera_entity;
				if (!can_delete_entity)
				{
					ImGui::BeginDisabled();
				}
				if (ImGui::Button("Delete"))
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
				if (ImGui::Button("Cancel"))
				{
					pending_delete_entity = INVALID_ENTITY;
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}

			if (delete_entity != INVALID_ENTITY)
			{
				if (picked_entity == delete_entity)
				{
					picked_entity = INVALID_ENTITY;
				}
				selected_index = -1;

				eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, delete_entity](uint64) {
					if (delete_entity == camera_entity)
					{
						return;
					}

					Vector<ecs::Entity> entities_to_delete;
					entities_to_delete.push_back(delete_entity);

					auto hierarchy_array = scene.GetComponentArray<ecs::HierarchyComponent>();
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

					DeferredEntityRemovalResources deferred_resources = {};
					deferred_resources.frames_left = 8;
					for (ecs::Entity entity : entities_to_delete)
					{
						ecs::GeometryComponent* geometry = scene.GetComponent<ecs::GeometryComponent>(entity);
						if (geometry && geometry->mesh)
						{
							deferred_resources.meshes.push_back(geometry->mesh);
							if (geometry->mesh->render_data.buffer)
							{
								deferred_resources.resources.push_back(geometry->mesh->render_data.buffer);
							}
							if (geometry->mesh->gpu_bvh.node_buffer)
							{
								deferred_resources.resources.push_back(geometry->mesh->gpu_bvh.node_buffer);
							}
							if (geometry->mesh->gpu_bvh.primitive_buffer)
							{
								deferred_resources.resources.push_back(geometry->mesh->gpu_bvh.primitive_buffer);
							}
						}

						ecs::MaterialComponent* material = scene.GetComponent<ecs::MaterialComponent>(entity);
						if (material)
						{
							for (ecs::MaterialSlot& material_slot : material->material_slots)
							{
								for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
								{
									if (material_slot.textures[texture_slot].texture)
									{
										deferred_resources.resources.push_back(material_slot.textures[texture_slot].texture);
									}
								}
							}
						}
					}

					if (!deferred_resources.meshes.empty() || !deferred_resources.resources.empty())
					{
						deferred_entity_removal_resources.push_back(std::move(deferred_resources));
					}

					scene.DestroyEntity(delete_entity);
					const Vector<ecs::Entity>& entities = scene.GetEntities();
					if (std::find(entities.begin(), entities.end(), picked_entity) == entities.end())
					{
						picked_entity = INVALID_ENTITY;
					}
					UpdateEntityList();
				});
			}
		}
		ImGui::End();

		if (ImGui::Begin("Inspector"))
		{
			if (picked_entity != INVALID_ENTITY)
			{
				// TODO: use reflection system
				NameComponent* name_comp = main_view.scene->GetComponent<NameComponent>(picked_entity);
				if (name_comp)
				{
					ImGui::PushID("NameComponent");
					ImGui::Text("NameComponent");
					bool remove_component = DrawComponentRemoveButton("NameComponent");

					if (!remove_component)
					{
						char name_buf[256];
						std::snprintf(name_buf, sizeof(name_buf), "%s", name_comp->value.c_str());

						if (ImGui::InputText("Value", name_buf, sizeof(name_buf)))
						{
							name_comp->value = name_buf;
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<NameComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				TransformComponent* transform_comp = main_view.scene->GetComponent<TransformComponent>(picked_entity);
				if (transform_comp)
				{
					ImGui::PushID("TransformComponent");
					ImGui::Text("TransformComponent");
					const bool can_remove_transform = picked_entity != camera_entity;
					bool remove_component = DrawComponentRemoveButton("TransformComponent", can_remove_transform);

					if (!remove_component)
					{
						float position[3] = { transform_comp->position.x, transform_comp->position.y, transform_comp->position.z };
						if (ImGui::InputFloat3("Position", position))
						{
							transform_comp->position = { position[0], position[1], position[2] };
							transform_comp->SetDirty();
						}

						float3 rotation_xyz = QuaternionToEulerXYZDegrees(transform_comp->rotation);
						float rotation[3] = { rotation_xyz.x, rotation_xyz.y, rotation_xyz.z };
						if (ImGui::InputFloat3("Rotation XYZ", rotation))
						{
							transform_comp->rotation = EulerXYZDegreesToQuaternion({ rotation[0], rotation[1], rotation[2] });
							transform_comp->SetDirty();
						}

						float scale[3] = { transform_comp->scale.x, transform_comp->scale.y, transform_comp->scale.z };
						if (ImGui::InputFloat3("Scale", scale))
						{
							transform_comp->scale = { scale[0], scale[1], scale[2] };
							transform_comp->SetDirty();
						}
					}
					else if (can_remove_transform)
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<TransformComponent>(entity);
							scene.SetBVHDirty();
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				HierarchyComponent* hierarchy_comp = main_view.scene->GetComponent<HierarchyComponent>(picked_entity);
				if (hierarchy_comp)
				{
					ImGui::PushID("HierarchyComponent");
					ImGui::Text("HierarchyComponent");
					bool remove_component = DrawComponentRemoveButton("HierarchyComponent");

					if (!remove_component)
					{
						uint64 parent_id = hierarchy_comp->parent_id;
						if (ImGui::InputScalar("Parent", ImGuiDataType_U64, &parent_id))
						{
							hierarchy_comp->parent_id = parent_id;
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<HierarchyComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				LightComponent* light_comp = main_view.scene->GetComponent<LightComponent>(picked_entity);
				if (light_comp)
				{
					ImGui::PushID("LightComponent");
					ImGui::Text("LightComponent");
					bool remove_component = DrawComponentRemoveButton("LightComponent");
					
					if (!remove_component)
					{
						// TODO: enum is hard coded
						int light_type = static_cast<int>(light_comp->type);
						const char* light_type_items[] = { "Directional", "Point", "Spot" };
						if (ImGui::Combo("Type", &light_type, light_type_items, IM_ARRAYSIZE(light_type_items)))
						{
							light_comp->type = static_cast<LightComponent::LightType>(light_type);
						}

						float color[3] = { light_comp->color.x, light_comp->color.y, light_comp->color.z };
						if (ImGui::InputFloat3("Color", color))
						{
							light_comp->color = { color[0], color[1], color[2] };
						}

						ImGui::DragFloat("Intensity", &light_comp->intensity, 1.0f, 0.0f, 100000.0f);
						ImGui::DragFloat("Range", &light_comp->range, 0.1f, 0.0f, 100000.0f);
						ImGui::DragFloat("Outer Cone", &light_comp->outer_cone_angle, 0.01f, 0.0f, math::PI);
						ImGui::DragFloat("Inner Cone", &light_comp->inner_cone_angle, 0.01f, 0.0f, math::PI);

						int shadow_map_resolution = static_cast<int>(light_comp->shadow_map_resolution);
						if (ImGui::InputInt("Shadow Resolution", &shadow_map_resolution))
						{
							light_comp->shadow_map_resolution = (std::max)(1, shadow_map_resolution);
						}

						int shadow_cascade_count = static_cast<int>(light_comp->shadow_cascade_count);
						if (ImGui::SliderInt("Cascade Count", &shadow_cascade_count, 1, SHADOW_CASCADE_COUNT_MAX))
						{
							light_comp->shadow_cascade_count = static_cast<uint32>(shadow_cascade_count);
						}

						ImGui::SliderFloat("Cascade Lambda", &light_comp->shadow_cascade_lambda, 0.0f, 1.0f);
						ImGui::SliderFloat("Cascade Blend", &light_comp->shadow_cascade_blend, 0.0f, 0.3f);

						bool is_active = light_comp->IsActive();
						if (ImGui::Checkbox("Active", &is_active))
						{
							light_comp->SetActive(is_active);
						}

						bool is_dynamic = light_comp->IsDynamic();
						if (ImGui::Checkbox("Dynamic", &is_dynamic))
						{
							light_comp->SetDynamic(is_dynamic);
						}

						bool is_cast_shadow = light_comp->IsCastShadow();
						if (ImGui::Checkbox("Cast Shadow", &is_cast_shadow))
						{
							light_comp->SetCastShadow(is_cast_shadow);
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<LightComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				CameraComponent* camera_comp = main_view.scene->GetComponent<CameraComponent>(picked_entity);
				if (camera_comp)
				{
					ImGui::PushID("CameraComponent");
					ImGui::Text("CameraComponent");
					const bool can_remove_camera = picked_entity != camera_entity;
					bool remove_component = DrawComponentRemoveButton("CameraComponent", can_remove_camera);

					if (!remove_component)
					{
						bool is_ortho = camera_comp->IsOrtho();
						if (ImGui::Checkbox("Orthographic", &is_ortho))
						{
							camera_comp->SetOrtho(is_ortho);
						}

						float near_plane = camera_comp->near_plane;
						float far_plane = camera_comp->far_plane;
						bool near_far_changed = false;
						near_far_changed |= ImGui::DragFloat("Near", &near_plane, 0.01f, 0.001f, 100000.0f);
						near_far_changed |= ImGui::DragFloat("Far", &far_plane, 1.0f, 0.01f, 100000.0f);
						if (near_far_changed)
						{
							near_plane = (std::max)(0.001f, near_plane);
							far_plane = (std::max)(near_plane + 0.001f, far_plane);
							camera_comp->SetNearFar(near_plane, far_plane);
						}

						if (!camera_comp->IsOrtho())
						{
							float fov_y = camera_comp->fov_y;
							if (ImGui::DragFloat("FOV Y", &fov_y, 0.01f, 0.01f, math::PI - 0.01f))
							{
								camera_comp->SetFOV_Y(fov_y);
							}
						}
						else
						{
							float ortho_vertical_size = camera_comp->ortho_vertical_size;
							if (ImGui::DragFloat("Ortho Size", &ortho_vertical_size, 0.1f, 0.001f, 100000.0f))
							{
								camera_comp->SetOrthoVerticalSize(ortho_vertical_size);
							}
						}

						ImGui::Text("Aspect Ratio: %.3f", camera_comp->aspect_ratio);
						ImGui::DragFloat("Aperture", &camera_comp->aperture, 0.01f, 0.0f, 128.0f);
						ImGui::DragFloat("Shutter Speed", &camera_comp->shutter_speed, 0.001f, 0.0001f, 100.0f);
						ImGui::DragFloat("Sensitivity", &camera_comp->sensitivity, 1.0f, 1.0f, 102400.0f);
					}
					else if (can_remove_camera)
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							if (entity != camera_entity)
							{
								scene.RemoveComponent<CameraComponent>(entity);
							}
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				SkyComponent* sky_comp = main_view.scene->GetComponent<SkyComponent>(picked_entity);
				if (sky_comp)
				{
					ImGui::PushID("SkyComponent");
					ImGui::Text("SkyComponent");
					bool remove_component = DrawComponentRemoveButton("SkyComponent");

					if (!remove_component)
					{
						bool is_active = sky_comp->IsActive();
						if (ImGui::Checkbox("Active", &is_active))
						{
							sky_comp->SetActive(is_active);
						}

						float sun_direction[3] = { sky_comp->sun_direction.x, sky_comp->sun_direction.y, sky_comp->sun_direction.z };
						if (ImGui::DragFloat3("Sun Direction", sun_direction, 0.01f, -1.0f, 1.0f))
						{
							float3 direction = { sun_direction[0], sun_direction[1], sun_direction[2] };
							const float direction_length_sq = math::LengthSquared(direction);
							if (direction_length_sq > 0.0f)
							{
								const float inv_direction_length = 1.0f / std::sqrt(direction_length_sq);
								sky_comp->sun_direction =
								{
									direction.x * inv_direction_length,
									direction.y * inv_direction_length,
									direction.z * inv_direction_length,
								};
							}
						}

						float sun_color[3] = { sky_comp->sun_color.x, sky_comp->sun_color.y, sky_comp->sun_color.z };
						if (ImGui::InputFloat3("Sun Color", sun_color))
						{
							sky_comp->sun_color = { sun_color[0], sun_color[1], sun_color[2] };
						}

						ImGui::DragFloat("Sun Intensity", &sky_comp->sun_intensity, 0.1f, 0.0f, 100000.0f);
						ImGui::DragFloat("Sun Angular Radius", &sky_comp->sun_angular_radius, 0.0001f, 0.0f, 1.0f);
						ImGui::DragFloat("Sun Glow Intensity", &sky_comp->sun_glow_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat("Sun Glow Falloff", &sky_comp->sun_glow_falloff, 0.1f, 0.0f, 1000.0f);

						float sky_horizon_color[3] = { sky_comp->sky_horizon_color.x, sky_comp->sky_horizon_color.y, sky_comp->sky_horizon_color.z };
						if (ImGui::InputFloat3("Sky Horizon Color", sky_horizon_color))
						{
							sky_comp->sky_horizon_color = { sky_horizon_color[0], sky_horizon_color[1], sky_horizon_color[2] };
						}

						float sky_zenith_color[3] = { sky_comp->sky_zenith_color.x, sky_comp->sky_zenith_color.y, sky_comp->sky_zenith_color.z };
						if (ImGui::InputFloat3("Sky Zenith Color", sky_zenith_color))
						{
							sky_comp->sky_zenith_color = { sky_zenith_color[0], sky_zenith_color[1], sky_zenith_color[2] };
						}

						ImGui::DragFloat("Sky Intensity", &sky_comp->sky_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat("Sky Horizon Falloff", &sky_comp->sky_horizon_falloff, 0.01f, 0.0f, 1000.0f);

						float ground_horizon_color[3] = { sky_comp->ground_horizon_color.x, sky_comp->ground_horizon_color.y, sky_comp->ground_horizon_color.z };
						if (ImGui::InputFloat3("Ground Horizon Color", ground_horizon_color))
						{
							sky_comp->ground_horizon_color = { ground_horizon_color[0], ground_horizon_color[1], ground_horizon_color[2] };
						}

						float ground_color[3] = { sky_comp->ground_color.x, sky_comp->ground_color.y, sky_comp->ground_color.z };
						if (ImGui::InputFloat3("Ground Color", ground_color))
						{
							sky_comp->ground_color = { ground_color[0], ground_color[1], ground_color[2] };
						}

						ImGui::DragFloat("Ground Intensity", &sky_comp->ground_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat("Ground Falloff", &sky_comp->ground_falloff, 0.01f, 0.0f, 1000.0f);
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<SkyComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				FogVolumeComponent* fog_volume_comp = main_view.scene->GetComponent<FogVolumeComponent>(picked_entity);
				if (fog_volume_comp)
				{
					ImGui::PushID("FogVolumeComponent");
					ImGui::Text("FogVolumeComponent");
					bool remove_component = DrawComponentRemoveButton("FogVolumeComponent");

					if (!remove_component)
					{

					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<FogVolumeComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				EnvironmentLightingComponent* environment_lighting_comp = main_view.scene->GetComponent<EnvironmentLightingComponent>(picked_entity);
				if (environment_lighting_comp)
				{
					ImGui::PushID("EnvironmentLightingComponent");
					ImGui::Text("EnvironmentLightingComponent");
					bool remove_component = DrawComponentRemoveButton("EnvironmentLightingComponent");

					if (!remove_component)
					{
						bool is_active = environment_lighting_comp->IsActive();
						if (ImGui::Checkbox("Active", &is_active))
						{
							environment_lighting_comp->SetActive(is_active);
						}

						int gi_mode = static_cast<int>(environment_lighting_comp->gi_mode);
						const char* gi_mode_items[] = { "None", "Ambient", "DDGI" };
						if (ImGui::Combo("GI Mode", &gi_mode, gi_mode_items, IM_ARRAYSIZE(gi_mode_items)))
						{
							environment_lighting_comp->gi_mode = static_cast<EnvironmentLightingComponent::GIMode>(gi_mode);
						}

						float ambient_color[3] = { environment_lighting_comp->ambient_color.x, environment_lighting_comp->ambient_color.y, environment_lighting_comp->ambient_color.z };
						if (ImGui::InputFloat3("Ambient Color", ambient_color))
						{
							environment_lighting_comp->ambient_color = { ambient_color[0], ambient_color[1], ambient_color[2] };
						}

						ImGui::DragFloat("Ambient Intensity", &environment_lighting_comp->ambient_intensity, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat("Indirect Diffuse Scale", &environment_lighting_comp->indirect_diffuse_scale, 0.01f, 0.0f, 1000.0f);
						ImGui::DragFloat("Indirect Specular Scale", &environment_lighting_comp->indirect_specular_scale, 0.01f, 0.0f, 1000.0f);
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<EnvironmentLightingComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				DDGIVolumeComponent* ddgi_volume_comp = main_view.scene->GetComponent<DDGIVolumeComponent>(picked_entity);
				if (ddgi_volume_comp)
				{
					ImGui::PushID("DDGIVolumeComponent");
					ImGui::Text("DDGIVolumeComponent");
					bool remove_component = DrawComponentRemoveButton("DDGIVolumeComponent");

					if (!remove_component)
					{
						bool is_active = ddgi_volume_comp->IsActive();
						if (ImGui::Checkbox("Active", &is_active))
						{
							ddgi_volume_comp->SetActive(is_active);
						}

						bool is_dynamic = ddgi_volume_comp->IsDynamic();
						if (ImGui::Checkbox("Dynamic", &is_dynamic))
						{
							ddgi_volume_comp->SetDynamic(is_dynamic);
						}

						int probe_counts[3] = {
							static_cast<int>(ddgi_volume_comp->probe_counts.x),
							static_cast<int>(ddgi_volume_comp->probe_counts.y),
							static_cast<int>(ddgi_volume_comp->probe_counts.z)
						};
						if (ImGui::InputInt3("Probe Counts", probe_counts))
						{
							ddgi_volume_comp->probe_counts = {
								static_cast<uint32>((std::max)(1, probe_counts[0])),
								static_cast<uint32>((std::max)(1, probe_counts[1])),
								static_cast<uint32>((std::max)(1, probe_counts[2]))
							};
						}

						float probe_spacing[3] = { ddgi_volume_comp->probe_spacing.x, ddgi_volume_comp->probe_spacing.y, ddgi_volume_comp->probe_spacing.z };
						if (ImGui::InputFloat3("Probe Spacing", probe_spacing))
						{
							ddgi_volume_comp->probe_spacing = { probe_spacing[0], probe_spacing[1], probe_spacing[2] };
						}

						float volume_offset[3] = { ddgi_volume_comp->volume_offset.x, ddgi_volume_comp->volume_offset.y, ddgi_volume_comp->volume_offset.z };
						if (ImGui::InputFloat3("Volume Offset", volume_offset))
						{
							ddgi_volume_comp->volume_offset = { volume_offset[0], volume_offset[1], volume_offset[2] };
						}

						int probes_per_frame = static_cast<int>(ddgi_volume_comp->probes_per_frame);
						if (ImGui::InputInt("Probes Per Frame", &probes_per_frame))
						{
							ddgi_volume_comp->probes_per_frame = static_cast<uint32>((std::max)(1, probes_per_frame));
						}

						int priority = static_cast<int>(ddgi_volume_comp->priority);
						if (ImGui::InputInt("Priority", &priority))
						{
							ddgi_volume_comp->priority = static_cast<uint32>((std::max)(0, priority));
						}

						ImGui::SliderFloat("Hysteresis", &ddgi_volume_comp->hysteresis, 0.0f, 1.0f);
						ImGui::DragFloat("Normal Bias", &ddgi_volume_comp->normal_bias, 0.001f, 0.0f, 100.0f);
						ImGui::DragFloat("View Bias", &ddgi_volume_comp->view_bias, 0.001f, 0.0f, 100.0f);
						ImGui::DragFloat("Max Distance", &ddgi_volume_comp->max_distance, 0.01f, 0.0f, 100000.0f);
						if (ImGui::Button("Update Scene GPUBVH"))
						{
							main_view.scene->BuildGPUBVH();
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<DDGIVolumeComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				GeometryComponent* geometry_comp = main_view.scene->GetComponent<GeometryComponent>(picked_entity);
				if (geometry_comp)
				{
					ImGui::PushID("GeometryComponent");
					ImGui::Text("GeometryComponent");
					bool remove_component = DrawComponentRemoveButton("GeometryComponent");

					if (!remove_component)
					{
						ImGui::Text("Mesh: %s", geometry_comp->mesh ? "Assigned" : "None");

						bool cast_shadow = geometry_comp->IsCastShadow();
						if (ImGui::Checkbox("Cast Shadow", &cast_shadow))
						{
							geometry_comp->SetCastShadow(cast_shadow);
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							DeferredEntityRemovalResources deferred_resources = {};
							deferred_resources.frames_left = 8;

							ecs::GeometryComponent* geometry = scene.GetComponent<ecs::GeometryComponent>(entity);
							if (geometry && geometry->mesh)
							{
								deferred_resources.meshes.push_back(geometry->mesh);
								if (geometry->mesh->render_data.buffer)
								{
									deferred_resources.resources.push_back(geometry->mesh->render_data.buffer);
								}
								if (geometry->mesh->gpu_bvh.node_buffer)
								{
									deferred_resources.resources.push_back(geometry->mesh->gpu_bvh.node_buffer);
								}
								if (geometry->mesh->gpu_bvh.primitive_buffer)
								{
									deferred_resources.resources.push_back(geometry->mesh->gpu_bvh.primitive_buffer);
								}
							}

							if (!deferred_resources.meshes.empty() || !deferred_resources.resources.empty())
							{
								deferred_entity_removal_resources.push_back(std::move(deferred_resources));
							}

							scene.RemoveComponent<GeometryComponent>(entity);
							scene.SetBVHDirty();
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Sprite3DComponent* sprite_3d_comp = main_view.scene->GetComponent<Sprite3DComponent>(picked_entity);
				if (sprite_3d_comp)
				{
					ImGui::PushID("Sprite3DComponent");
					ImGui::Text("Sprite3DComponent");
					bool remove_component = DrawComponentRemoveButton("Sprite3DComponent");

					if (!remove_component)
					{
						float size[2] = { sprite_3d_comp->size.x, sprite_3d_comp->size.y };
						if (ImGui::InputFloat2("Size", size))
						{
							sprite_3d_comp->size = { size[0], size[1] };
							sprite_3d_comp->SetDirty();
						}

						float pivot[2] = { sprite_3d_comp->pivot.x, sprite_3d_comp->pivot.y };
						if (ImGui::InputFloat2("Pivot", pivot))
						{
							sprite_3d_comp->pivot = { pivot[0], pivot[1] };
							sprite_3d_comp->SetDirty();
						}

						float uv_rect[4] = { sprite_3d_comp->uv_rect.x, sprite_3d_comp->uv_rect.y, sprite_3d_comp->uv_rect.z, sprite_3d_comp->uv_rect.w };
						if (ImGui::InputFloat4("UV Rect", uv_rect))
						{
							sprite_3d_comp->uv_rect = { uv_rect[0], uv_rect[1], uv_rect[2], uv_rect[3] };
							sprite_3d_comp->SetDirty();
						}

						bool billboard = sprite_3d_comp->IsBillboard();
						if (ImGui::Checkbox("Billboard", &billboard))
						{
							sprite_3d_comp->SetBillboard(billboard);
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<Sprite3DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				Text3DComponent* text_3d_comp = main_view.scene->GetComponent<Text3DComponent>(picked_entity);
				if (text_3d_comp)
				{
					ImGui::PushID("Text3DComponent");
					ImGui::Text("Text3DComponent");
					bool remove_component = DrawComponentRemoveButton("Text3DComponent");

					if (!remove_component)
					{
						ImGui::Text("Font: %s", text_3d_comp->font && text_3d_comp->font->IsValid() ? "Assigned" : "None");

						char text_buf[4096] = {};
						strncpy_s(text_buf, text_3d_comp->text.c_str(), sizeof(text_buf) - 1);
						if (ImGui::InputTextMultiline("Text", text_buf, sizeof(text_buf), ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 4.0f)))
						{
							text_3d_comp->text = text_buf;
							text_3d_comp->SetDirty();
						}

						int pixel_height = static_cast<int>(text_3d_comp->pixel_height);
						if (ImGui::InputInt("Pixel Height", &pixel_height))
						{
							text_3d_comp->pixel_height = static_cast<uint32>((std::max)(1, pixel_height));
							text_3d_comp->SetDirty();
						}

						if (ImGui::InputFloat("Height", &text_3d_comp->height))
						{
							text_3d_comp->height = (std::max)(0.0f, text_3d_comp->height);
							text_3d_comp->SetDirty();
						}

						float pivot[2] = { text_3d_comp->pivot.x, text_3d_comp->pivot.y };
						if (ImGui::InputFloat2("Pivot", pivot))
						{
							text_3d_comp->pivot = { pivot[0], pivot[1] };
							text_3d_comp->SetDirty();
						}

						bool billboard = text_3d_comp->IsBillboard();
						if (ImGui::Checkbox("Billboard", &billboard))
						{
							text_3d_comp->SetBillboard(billboard);
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<Text3DComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				AnimationComponent* animation_comp = main_view.scene->GetComponent<AnimationComponent>(picked_entity);
				if (animation_comp)
				{
					ImGui::PushID("AnimationComponent");
					ImGui::Text("AnimationComponent");
					bool remove_component = DrawComponentRemoveButton("AnimationComponent");

					if (!remove_component)
					{
						const int clip_count = static_cast<int>(animation_comp->clips.size());
						ImGui::Text("Clips: %d", clip_count);

						if (clip_count > 0)
						{
							if (animation_comp->current_clip_index >= animation_comp->clips.size())
							{
								animation_comp->current_clip_index = 0;
							}

							int current_clip_index = static_cast<int>(animation_comp->current_clip_index);
							std::shared_ptr<resource::AnimationClip> current_clip = animation_comp->clips[animation_comp->current_clip_index];
							String current_clip_name = current_clip && !current_clip->name.empty() ? current_clip->name : "Clip " + std::to_string(current_clip_index);
							if (ImGui::BeginCombo("Clip", current_clip_name.c_str()))
							{
								for (int clip_index = 0; clip_index < clip_count; ++clip_index)
								{
									const std::shared_ptr<resource::AnimationClip>& clip = animation_comp->clips[clip_index];
									String clip_name = clip && !clip->name.empty() ? clip->name : "Clip " + std::to_string(clip_index);
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

							if (ImGui::Button(animation_comp->playing ? "Pause" : "Play"))
							{
								animation_comp->playing = !animation_comp->playing;
							}
							ImGui::SameLine();
							ImGui::Checkbox("Loop", &animation_comp->loop);

							ImGui::DragFloat("Speed", &animation_comp->speed, 0.01f, -10.0f, 10.0f);

							if (duration_seconds > 0.0f)
							{
								float time = std::clamp(animation_comp->time, 0.0f, duration_seconds);
								if (ImGui::SliderFloat("Time", &time, 0.0f, duration_seconds))
								{
									animation_comp->time = time;
									animation_comp->bone_matrices_dirty = true;
								}
								ImGui::Text("Duration: %.3fs", duration_seconds);
							}
							else
							{
								ImGui::TextDisabled("Duration: 0.000s");
							}
						}
						else
						{
							ImGui::TextDisabled("No animation clips");
						}

						ImGui::Text("Bone Matrices: %d", static_cast<int>(animation_comp->bone_matrices.size()));
						ImGui::Text("Bone Matrix Offset: %u", animation_comp->bone_matrix_offset);
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							scene.RemoveComponent<AnimationComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				MaterialComponent* material_comp = main_view.scene->GetComponent<MaterialComponent>(picked_entity);
				if (material_comp)
				{
					ImGui::PushID("MaterialComponent");
					ImGui::Text("MaterialComponent");
					bool remove_component = DrawComponentRemoveButton("MaterialComponent");

					if (!remove_component)
					{
						static Entity selected_material_entity = INVALID_ENTITY;
						static int selected_material_slot = 0;
						if (selected_material_entity != picked_entity)
						{
							selected_material_entity = picked_entity;
							selected_material_slot = 0;
						}

						int material_slot_count = static_cast<int>(material_comp->GetMaterialSlotCount());
						ImGui::Text("Material Slots: %d", material_slot_count);

						if (ImGui::Button("Add Slot"))
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
						if (ImGui::Button("Remove Slot") && material_slot_count > 0)
						{
							material_comp->material_slots.erase(material_comp->material_slots.begin() + selected_material_slot);
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

							String slot_label = "Slot " + std::to_string(selected_material_slot);
							if (ImGui::BeginCombo("Selected Slot", slot_label.c_str()))
							{
								for (int slot_index = 0; slot_index < material_slot_count; ++slot_index)
								{
									String item_label = "Slot " + std::to_string(slot_index);
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

							MaterialSlot& material_slot = material_comp->GetMaterialSlot(static_cast<uint32>(selected_material_slot));
							const char* shader_type_items[] = { "Unlit" };
							int shader_type = static_cast<int>(material_slot.shader_type);
							if (ImGui::Combo("Shader Type", &shader_type, shader_type_items, IM_ARRAYSIZE(shader_type_items)))
							{
								material_slot.shader_type = static_cast<uint32>(shader_type);
							}

							bool is_double_sided = (material_slot.flags & SHADER_MATERIAL_FLAG_DOUBLE_SIDED) != 0;
							if (ImGui::Checkbox("Double Sided", &is_double_sided))
							{
								if (is_double_sided) { material_slot.flags |= SHADER_MATERIAL_FLAG_DOUBLE_SIDED; } else { material_slot.flags &= ~SHADER_MATERIAL_FLAG_DOUBLE_SIDED; }
							}

							bool is_transparent = (material_slot.flags & SHADER_MATERIAL_FLAG_TRANSPARENT) != 0;
							if (ImGui::Checkbox("Transparent", &is_transparent))
							{
								if (is_transparent) { material_slot.flags |= SHADER_MATERIAL_FLAG_TRANSPARENT; } else { material_slot.flags &= ~SHADER_MATERIAL_FLAG_TRANSPARENT; }
							}

							bool use_vertex_colors = (material_slot.flags & SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS) != 0;
							if (ImGui::Checkbox("Use Vertex Colors", &use_vertex_colors))
							{
								if (use_vertex_colors) { material_slot.flags |= SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS; } else { material_slot.flags &= ~SHADER_MATERIAL_FLAG_USE_VERTEX_COLORS; }
							}

							bool receive_shadow = (material_slot.flags & SHADER_MATERIAL_FLAG_RECEIVE_SHADOW) != 0;
							if (ImGui::Checkbox("Receive Shadow", &receive_shadow))
							{
								if (receive_shadow) { material_slot.flags |= SHADER_MATERIAL_FLAG_RECEIVE_SHADOW; } else { material_slot.flags &= ~SHADER_MATERIAL_FLAG_RECEIVE_SHADOW; }
							}

							float base_color[4] = { material_slot.base_color.x, material_slot.base_color.y, material_slot.base_color.z, material_slot.base_color.w };
							if (ImGui::ColorEdit4("Base Color", base_color))
							{
								material_slot.base_color = { base_color[0], base_color[1], base_color[2], base_color[3] };
							}

							ImGui::SliderFloat("Metallic", &material_slot.metallic, 0.0f, 1.0f);
							ImGui::SliderFloat("Roughness", &material_slot.roughness, 0.0f, 1.0f);
							ImGui::SliderFloat("Reflectance", &material_slot.reflectance, 0.0f, 1.0f);
							ImGui::SliderFloat("Anisotropy", &material_slot.anisotropy, -1.0f, 1.0f);

							float sheen_color[3] = { material_slot.sheen_color.x, material_slot.sheen_color.y, material_slot.sheen_color.z };
							if (ImGui::ColorEdit3("Sheen Color", sheen_color))
							{
								material_slot.sheen_color = { sheen_color[0], sheen_color[1], sheen_color[2] };
							}

							ImGui::SliderFloat("Sheen Roughness", &material_slot.sheen_roughness, 0.0f, 1.0f);
							ImGui::SliderFloat("Clearcoat", &material_slot.clearcoat, 0.0f, 1.0f);
							ImGui::SliderFloat("Clearcoat Roughness", &material_slot.clearcoat_roughness, 0.0f, 1.0f);

							static const char* texture_slot_names[] = {
								"Base Color Map",
								"Normal Map",
								"Emissive Map",
								"Opacity Map",
								"Displacement Map",
								"Occlusion Map",
								"Sheen Color Map",
								"Sheen Roughness Map",
								"Clearcoat Map",
								"Clearcoat Roughness Map",
								"Clearcoat Normal Map",
								"Anisotropy Map",
								"Roughness Map",
								"Metallic Map",
							};
							static_assert(TEXTURESLOT_COUNT == IM_ARRAYSIZE(texture_slot_names), "Texture slot labels must match");

							ImGui::SeparatorText("Textures");
							for (uint32 texture_slot = 0; texture_slot < static_cast<uint32>(TEXTURESLOT_COUNT); ++texture_slot)
							{
								const MaterialSlot::TextureMap& texture = material_slot.textures[texture_slot];
								ImGui::Text("%s: %s", texture_slot_names[texture_slot], texture.IsValid() ? "Assigned" : "None");
							}
						}
					}
					else
					{
						const ecs::Entity entity = picked_entity;
						eventhandler::SubscribeOnce(eventhandler::EVENT_THREAD_SAFE_POINT, [this, entity](uint64) {
							DeferredEntityRemovalResources deferred_resources = {};
							deferred_resources.frames_left = 8;

							ecs::MaterialComponent* material = scene.GetComponent<ecs::MaterialComponent>(entity);
							if (material)
							{
								for (ecs::MaterialSlot& material_slot : material->material_slots)
								{
									for (uint32 texture_slot = 0; texture_slot < TEXTURESLOT_COUNT; ++texture_slot)
									{
										if (material_slot.textures[texture_slot].texture)
										{
											deferred_resources.resources.push_back(material_slot.textures[texture_slot].texture);
										}
									}
								}
							}

							if (!deferred_resources.meshes.empty() || !deferred_resources.resources.empty())
							{
								deferred_entity_removal_resources.push_back(std::move(deferred_resources));
							}

							scene.RemoveComponent<MaterialComponent>(entity);
						});
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				if (ImGui::Button("Add Component", ImVec2(-1.0f, 0.0f)))
				{
					ImGui::OpenPopup("AddComponentPopup");
				}

				if (ImGui::BeginPopup("AddComponentPopup"))
				{
					if (ImGui::MenuItem("NameComponent"))
					{
						if (main_view.scene->GetComponent<NameComponent>(picked_entity) == nullptr)
						{
							if (NameComponent* name = main_view.scene->AddComponent<NameComponent>(picked_entity))
							{
								name->value = "Entity " + std::to_string(picked_entity);
							}
						}
					}

					if (ImGui::MenuItem("TransformComponent"))
					{
						if (main_view.scene->GetComponent<TransformComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<TransformComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("HierarchyComponent"))
					{
						if (main_view.scene->GetComponent<HierarchyComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<HierarchyComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("CameraComponent"))
					{
						if (main_view.scene->GetComponent<CameraComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<CameraComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("LightComponent"))
					{
						if (main_view.scene->GetComponent<LightComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<LightComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("SkyComponent"))
					{
						if (main_view.scene->GetComponent<SkyComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<SkyComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("FogVolumeComponent"))
					{
						if (main_view.scene->GetComponent<FogVolumeComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<FogVolumeComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("EnvironmentLightingComponent"))
					{
						if (main_view.scene->GetComponent<EnvironmentLightingComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<EnvironmentLightingComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("DDGIVolumeComponent"))
					{
						if (main_view.scene->GetComponent<DDGIVolumeComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<DDGIVolumeComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("GeometryComponent"))
					{
						if (main_view.scene->GetComponent<GeometryComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<GeometryComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("Sprite3DComponent"))
					{
						if (main_view.scene->GetComponent<Sprite3DComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<Sprite3DComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("Text3DComponent"))
					{
						if (main_view.scene->GetComponent<Text3DComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<Text3DComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("AnimationComponent"))
					{
						if (main_view.scene->GetComponent<AnimationComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<AnimationComponent>(picked_entity);
						}
					}

					if (ImGui::MenuItem("MaterialComponent"))
					{
						if (main_view.scene->GetComponent<MaterialComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<MaterialComponent>(picked_entity);
						}
					}

					ImGui::EndPopup();
				}
			}
		}
		ImGui::End();

		if (ImGui::Begin("Log", nullptr, ImGuiWindowFlags_NoScrollbar))
		{
			static std::string lastlog = "";

			//ImGui::SameLine();
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));
			if (ImGui::Button("Copy to Clipboard", ImVec2(DEFAULTBUTTONWIDTH, 0)))
				ImGui::SetClipboardText(lastlog.c_str());
			ImGui::SameLine();
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));
			if (ImGui::Button("Clear", ImVec2(DEFAULTBUTTONWIDTH, 0)))
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

		if (ImGui::Begin("Contents Browser", nullptr))
		{
			DrawContentsBrowser();
		}
		ImGui::End();

		if (ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));

			static bool profiler_enabled = false;
			static double last_profiler_ui_update_time = -1.0;
			static std::string cached_performance;
			static std::string cached_res_usage;
			if (ImGui::Checkbox("Enable Profiler", &profiler_enabled))
			{
				profiler::SetEnabled(profiler_enabled);
				last_profiler_ui_update_time = -1.0;
				cached_performance = profiler_enabled ? "Profiler starting..." : "";
				cached_res_usage.clear();
			}

			if (profiler_enabled)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Profiler Turned On! Performance may be reduced!");

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

	void EditorApplication::LoadSampleScene()
	{
		{
			std::shared_ptr<resource::Font> noto_sans_font = resource::LoadFontFile(contents_root_dir + "Fonts/Noto_Sans_KR/static/NotoSansKR-Regular.ttf");
			if (noto_sans_font && noto_sans_font->IsValid())
			{
				ecs::Entity text_entity = scene.CreateEntity();
				if (auto* transform = scene.AddComponent<ecs::TransformComponent>(text_entity))
				{
					transform->position = { 0.0f, 3.0f, 0.0f };
					transform->SetDirty();
				}

				if (auto* text = scene.AddComponent<ecs::Text3DComponent>(text_entity))
				{
					text->font = noto_sans_font;
					//text->text = "\xED\x85\x8C\xEC\x8A\xA4\xED\x8A\xB8";
					text->text = "Test1234!@#$";
					text->pixel_height = 64;
					text->height = 0.3f;
					text->pivot = { 0.5f, 0.5f };
					text->SetBillboard(true);
				}

				if (auto* material = scene.AddComponent<ecs::MaterialComponent>(text_entity))
				{

				}

				if (auto* name = scene.AddComponent<ecs::NameComponent>(text_entity))
				{
					name->value = "Text Test";
				}
			}

			String file_path = contents_root_dir + "/Images/env_comp.png";
			std::shared_ptr<resource::Image> image = resource::LoadImageFile(file_path, 4);
			if (image && image->IsValid())
			{
				rendering::utils::CreateRenderData(*device, *image, RHIFormat::R8G8B8A8Unorm, true);
				if (image->render_data.IsValid())
				{
					ecs::Entity sprite_entity = scene.CreateEntity();
					if (auto* transform = scene.AddComponent<ecs::TransformComponent>(sprite_entity))
					{
						transform->position = { 0.0f, 2.0f, 0.0f };
						transform->SetDirty();
					}

					if (auto* sprite = scene.AddComponent<ecs::Sprite3DComponent>(sprite_entity))
					{
						const float sprite_height = 2.0f;
						const float sprite_aspect = static_cast<float>(image->width) / static_cast<float>(image->height);
						sprite->size = { sprite_height * sprite_aspect, sprite_height };
						sprite->SetBillboard(false);
					}

					if (auto* material = scene.AddComponent<ecs::MaterialComponent>(sprite_entity))
					{
						MaterialSlot& material_slot = material->AddMaterialSlot();
						material_slot.flags = SHADER_MATERIAL_FLAG_TRANSPARENT;
						material_slot.base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
						material_slot.textures[BASECOLORMAP].name = "Test Sprite BaseColorMap";
						material_slot.textures[BASECOLORMAP].texture = image->render_data.texture;
						material_slot.textures[BASECOLORMAP].res_handle = image->render_data.srv;
					}

					if (auto* name = scene.AddComponent<ecs::NameComponent>(sprite_entity))
					{
						name->value = "Test 3D Sprite";
					}
				}
			}

			//String file_path = io::GetWorkingDirectory() + "/../Contents/Images/env_comp.png";
			//std::shared_ptr<resource::Image> image = resource::LoadImageFile(file_path, 4);
			//if (!image || !image->IsValid())
			//{
			//	backlog::Post(String("failed to load base color texture: ") + file_path, backlog::LogLevel::Warning);
			//	return;
			//}

			//RHITextureDesc texture_desc = {};
			//texture_desc.width = static_cast<uint32>(image->width);
			//texture_desc.height = static_cast<uint32>(image->height);
			//texture_desc.depth = 1;
			//texture_desc.mip_levels = 1;
			//texture_desc.array_layers = 1;
			//texture_desc.sample_count = 1;
			//texture_desc.format = RHIFormat::R8G8B8A8Unorm;
			//texture_desc.usage = RHIResourceUsage::Default;
			//texture_desc.bind_flags = RHIBindFlags::ShaderResource;

			//std::shared_ptr<RHIResource> texture_resource = device->CreateTexture(texture_desc, image->pixels.data(), image->pixels.size());
			//if (!texture_resource)
			//{
			//	backlog::Post("failed to create base color texture resource", backlog::LogLevel::Warning);
			//	return;
			//}

			//RHISubresourceDesc texture_srv_desc = {};
			//texture_srv_desc.type = RHISubresourceType::ShaderResource;
			//texture_srv_desc.first_slice = 0;
			//texture_srv_desc.slice_count = 1;
			//texture_srv_desc.first_mip = 0;
			//texture_srv_desc.mip_count = 1;

			//RHISubresourceHandle texture_srv = {};
			//if (!device->CreateSubresource(*texture_resource, texture_srv_desc, &texture_srv))
			//{
			//	backlog::Post("failed to create base color texture srv", backlog::LogLevel::Warning);
			//	return;
			//}

			//// image entity
			//{
			//	image_entity = scene.CreateEntity();

			//	auto* transform = scene.AddComponent<ecs::TransformComponent>(image_entity);
			//	if (transform)
			//	{
			//		transform->position = { 0.0f, 0.0f, 0.0f };
			//		transform->Scale({ 2.f,2.f,2.f });
			//	}

			//	auto* geometry = scene.AddComponent<ecs::GeometryComponent>(image_entity);
			//	if (geometry)
			//	{
			//		auto mesh = std::make_shared<resource::Mesh>();
			//		mesh->positions = {
			//			{ 0.0f, 3.0f, 1.0f },
			//			{ 3.0f, 0.0f, 1.0f },
			//			{ -3.0f, 0.0f, 1.0f },
			//		};
			//		mesh->normals = {
			//			{ 0.0f, 0.0f, -1.f },
			//			{ 0.0f, 0.0f, -1.f },
			//			{ 0.0f, 0.0f, -1.f },
			//		};
			//		mesh->texcoords = {
			//			{ 0.5f, 0.0f },
			//			{ 1.0f, 1.0f },
			//			{ 0.0f, 1.0f },
			//		};

			//		mesh->indices = { 0, 1, 2 };

			//		resource::Submesh submesh = {};
			//		submesh.first_index = 0;
			//		//submesh.index_count = 3;
			//		submesh.index_count = 3;
			//		submesh.first_vertex = 0;
			//		submesh.material_slot = 0;
			//		submesh.local_bounds.min = { -0.5f, -0.5f, 0.0f };
			//		submesh.local_bounds.max = { 0.5f, 0.5f, 0.0f };
			//		mesh->submeshes.push_back(submesh);

			//		geometry->mesh = mesh;
			//		geometry->local_bounds = submesh.local_bounds;

			//		rendering::utils::CreateRenderData(*device, *mesh);
			//	}

			//	auto* material = scene.AddComponent<ecs::MaterialComponent>(image_entity);
			//	if (material)
			//	{
			//		auto& material_slot = material->AddMaterialSlot();
			//		material_slot.base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
			//		material_slot.metallic = 0.0f;
			//		material_slot.roughness = 1.0f;
			//		//material_slot.textures[0].name = "Test BaseColorMap";
			//		//material_slot.textures[0].texture = texture_resource;
			//		//material_slot.textures[0].res_handle = texture_srv;
			//	}
			//}

			//// light entity
			//{
			//	ecs::Entity light_entity = scene.CreateEntity();
			//	auto* transform = scene.AddComponent<ecs::TransformComponent>(light_entity);
			//	//transform->RotateRollPitchYaw({ - math::PI / 12.f, 0, 0});
			//	transform->Translate({ 0,0,-1 });
			//	auto* light = scene.AddComponent<ecs::LightComponent>(light_entity);
			//	light->type = ecs::LightComponent::LightType::Point;
			//	light->intensity = 100.f;
			//	light->range = 20.f;
			//	light->outer_cone_angle = math::PI / 3.f;
			//	light->inner_cone_angle = math::PI / 6.f;
			//}
		}

		{
			auto asset_importer = plugin_manager->GetPlugin(WON_IID_ASSET_IMPORTER);
			AssetImporterAPI* api = (AssetImporterAPI*)asset_importer->QueryInterface(WON_IID_ASSET_IMPORTER, WON_VID_ASSET_IMPORTER);

			std::string file_path = contents_root_dir + "/Models/glTF/Sponza/glTF/Sponza.gltf";
			//std::string file_path = contents_root_dir + "/Models/Obj/Sphere/sphere.obj";
			if (std::shared_ptr<AssetImportTask> import_task = api->ImportAsync(asset_importer.get(), file_path.c_str(), &scene, device.get()))
			{
				asset_import_tasks.push_back(import_task);
			}

			std::string cesium_man_file_path = contents_root_dir + "/Models/glTF/CesiumMan/glTF/CesiumMan.gltf";
			if (std::shared_ptr<AssetImportTask> cesium_man_import_task = api->ImportAsync(asset_importer.get(), cesium_man_file_path.c_str(), &scene, device.get()))
			{
				asset_import_tasks.push_back(cesium_man_import_task);
			}

			//ecs::Entity root_entity{};
			//api->Import(asset_importer.get(), file_path.c_str(), &scene, device.get(), root_entity);

			//{
			//	auto transform = scene.GetComponent<ecs::TransformComponent>(root_entity);
			//	if (transform)
			//	{
			//		transform->Translate({ 5.0f, 0.0f, 5.0f });
			//		transform->Scale({ 3.0f, 3.0f, 3.0f });
			//	}

			//	auto material_component = scene.GetComponent<ecs::MaterialComponent>(root_entity);
			//	for (uint32 i = 0; i < (uint32)material_component->GetMaterialSlotCount(); i++)
			//	{
			//		//auto& slot = material_component->GetMaterialSlot(i);
			//		//slot.shader_type =
			//	}
			//	auto geometry_component = scene.GetComponent<ecs::GeometryComponent>(root_entity);
			//	geometry_component->SetCastShadow(true);
			//}

			// light entity
			{
				ecs::Entity light_entity = scene.CreateEntity();
				auto transform = scene.AddComponent<ecs::TransformComponent>(light_entity);
				{
					const XMVECTOR source_direction = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
					const XMVECTOR target_direction = XMVector3Normalize(XMVectorSet(1.0f, -1.0f, 1.0f, 0.0f));
					const XMVECTOR rotation_axis = XMVector3Normalize(XMVector3Cross(source_direction, target_direction));
					const float rotation_angle = std::acos((std::max)(-1.0f, (std::min)(1.0f, XMVectorGetX(XMVector3Dot(source_direction, target_direction)))));
					XMStoreFloat4(&transform->rotation, XMQuaternionRotationAxis(rotation_axis, rotation_angle));
					transform->SetDirty();
				}
				auto light = scene.AddComponent<ecs::LightComponent>(light_entity);
				light->type = ecs::LightComponent::LightType::Directional;
				light->intensity = 100.f;
				//light->range = 20.f;
				//light->outer_cone_angle = math::PI / 3.f;
				//light->inner_cone_angle = math::PI / 6.f;

				auto name = scene.AddComponent<ecs::NameComponent>(light_entity);
				name->value = "Light";
			}

			// environment entity
			{
				ecs::Entity env_entity = scene.CreateEntity();
				
				auto env = scene.AddComponent<ecs::SkyComponent>(env_entity);
				env->SetActive(true);
				auto environment_lighting = scene.AddComponent<ecs::EnvironmentLightingComponent>(env_entity);
				environment_lighting->gi_mode = ecs::EnvironmentLightingComponent::DDGI;
				environment_lighting->indirect_diffuse_scale = 1.f;
				auto ddgi_volume = scene.AddComponent<ecs::DDGIVolumeComponent>(env_entity);
				ddgi_volume->probe_counts = { 16, 16, 16 };
				ddgi_volume->probe_spacing = { 2.0f, 2.0f, 2.0f };
				ddgi_volume->max_distance = 4.f;
				ddgi_volume->probes_per_frame = 128u;
				//ddgi_volume->volume_offset = { 5.0f, 0.0f, 5.0f };

				auto name = scene.AddComponent<ecs::NameComponent>(env_entity);
				name->value = "Environment";
			}

			//// plane entity
			//{
			//	ecs::Entity plane_entity = scene.CreateEntity();
			//	auto transform = scene.AddComponent<ecs::TransformComponent>(plane_entity);
			//	if (transform)
			//	{
			//		transform->Translate({ 0.f, -5.f, 0.f });
			//		transform->Scale({ 10.f, 10.f, 10.f });
			//	}
			//	auto geometry = scene.AddComponent<ecs::GeometryComponent>(plane_entity);
			//	if (geometry)
			//	{
			//		//geometry->SetCastShadow(true);

			//		auto mesh = std::make_shared<resource::Mesh>();
			//		mesh->positions = {
			//			{ 1.0f, 0.0f, 1.0f },
			//			{ -1.0f, 0.0f, 1.0f },
			//			{ 1.0f, 0.0f, -1.0f },
			//			{ -1.0f, 0.0f, -1.0f },
			//		};
			//		mesh->normals = {
			//			{ 0.0f, 1.0f, 0.f },
			//			{ 0.0f, 1.0f, 0.f },
			//			{ 0.0f, 1.0f, 0.f },
			//			{ 0.0f, 1.0f, 0.f },
			//		};

			//		mesh->indices = { 1, 0, 2, 1, 2, 3 };

			//		resource::Submesh submesh = {};
			//		submesh.first_index = 0;
			//		submesh.index_count = 6;
			//		submesh.first_vertex = 0;
			//		submesh.material_slot = 0;
			//		submesh.local_bounds.min = { -1.0f, 0.0f, -1.0f };
			//		submesh.local_bounds.max = { 1.0f, 0.0f, 1.0f };
			//		mesh->submeshes.push_back(submesh);

			//		geometry->SetMesh(mesh);

			//		rendering::utils::CreateRenderData(*device, *mesh);
			//	}

			//	auto material = scene.AddComponent<ecs::MaterialComponent>(plane_entity);
			//	if (material)
			//	{
			//		auto& material_slot = material->AddMaterialSlot();
			//		material_slot.base_color = { 0.70f, 0.82f, 0.68f, 1.0f };
			//		material_slot.metallic = 0.0f;
			//		material_slot.roughness = 0.5f;
			//		material_slot.flags |= SHADER_MATERIAL_FLAG_RECEIVE_SHADOW;
			//	}

			//	auto name = scene.AddComponent<ecs::NameComponent>(plane_entity);
			//	name->value = "Plane";

			//}

			//// side wall plane entity
			//{
			//	ecs::Entity side_wall_entity = scene.CreateEntity();
			//	scene.AddComponent<ecs::TransformComponent>(side_wall_entity);

			//	auto geometry = scene.AddComponent<ecs::GeometryComponent>(side_wall_entity);
			//	if (geometry)
			//	{
			//		auto mesh = std::make_shared<resource::Mesh>();
			//		mesh->positions = {
			//			{ 10.0f, 15.0f, 10.0f },
			//			{ 10.0f, -5.0f, 10.0f },
			//			{ 10.0f, 15.0f, -10.0f },
			//			{ 10.0f, -5.0f, -10.0f },
			//		};
			//		mesh->normals = {
			//			{ -1.0f, 0.0f, 0.0f },
			//			{ -1.0f, 0.0f, 0.0f },
			//			{ -1.0f, 0.0f, 0.0f },
			//			{ -1.0f, 0.0f, 0.0f },
			//		};

			//		mesh->indices = { 1, 0, 2, 1, 2, 3 };

			//		resource::Submesh submesh = {};
			//		submesh.first_index = 0;
			//		submesh.index_count = 6;
			//		submesh.first_vertex = 0;
			//		submesh.material_slot = 0;
			//		submesh.local_bounds.min = { 9.999f, -5.0f, -10.0f };
			//		submesh.local_bounds.max = { 10.001f, 15.0f, 10.0f };
			//		mesh->submeshes.push_back(submesh);

			//		geometry->SetMesh(mesh);
			//		rendering::utils::CreateRenderData(*device, *mesh);
			//	}

			//	auto material = scene.AddComponent<ecs::MaterialComponent>(side_wall_entity);
			//	if (material)
			//	{
			//		auto& material_slot = material->AddMaterialSlot();
			//		material_slot.base_color = { 0.9f, 0.35f, 0.35f, 1.0f };
			//		material_slot.metallic = 0.0f;
			//		material_slot.roughness = 0.5f;
			//		material_slot.flags |= SHADER_MATERIAL_FLAG_RECEIVE_SHADOW;
			//	}

			//	auto name = scene.AddComponent<ecs::NameComponent>(side_wall_entity);
			//	name->value = "Side Wall";
			//}

			//// back wall plane entity
			//{
			//	ecs::Entity back_wall_entity = scene.CreateEntity();
			//	scene.AddComponent<ecs::TransformComponent>(back_wall_entity);

			//	auto geometry = scene.AddComponent<ecs::GeometryComponent>(back_wall_entity);
			//	if (geometry)
			//	{
			//		auto mesh = std::make_shared<resource::Mesh>();
			//		mesh->positions = {
			//			{ -10.0f, 15.0f, 10.0f },
			//			{ -10.0f, -5.0f, 10.0f },
			//			{ 10.0f, 15.0f, 10.0f },
			//			{ 10.0f, -5.0f, 10.0f },
			//		};
			//		mesh->normals = {
			//			{ 0.0f, 0.0f, -1.0f },
			//			{ 0.0f, 0.0f, -1.0f },
			//			{ 0.0f, 0.0f, -1.0f },
			//			{ 0.0f, 0.0f, -1.0f },
			//		};

			//		mesh->indices = { 1, 0, 2, 1, 2, 3 };

			//		resource::Submesh submesh = {};
			//		submesh.first_index = 0;
			//		submesh.index_count = 6;
			//		submesh.first_vertex = 0;
			//		submesh.material_slot = 0;
			//		submesh.local_bounds.min = { -10.0f, -5.0f, 9.999f };
			//		submesh.local_bounds.max = { 10.0f, 15.0f, 10.001f };
			//		mesh->submeshes.push_back(submesh);

			//		geometry->SetMesh(mesh);
			//		rendering::utils::CreateRenderData(*device, *mesh);
			//	}

			//	auto material = scene.AddComponent<ecs::MaterialComponent>(back_wall_entity);
			//	if (material)
			//	{
			//		auto& material_slot = material->AddMaterialSlot();
			//		material_slot.base_color = { 0.35f, 0.45f, 0.9f, 1.0f };
			//		material_slot.metallic = 0.0f;
			//		material_slot.roughness = 0.5f;
			//		material_slot.flags |= SHADER_MATERIAL_FLAG_RECEIVE_SHADOW;
			//	}

			//	auto name = scene.AddComponent<ecs::NameComponent>(back_wall_entity);
			//	name->value = "Back Wall";
			//}
		}
		UpdateEntityList();
	}
	void EditorApplication::UpdateEntityList()
	{
		static std::mutex entity_list_mutex;
		std::lock_guard<std::mutex> lock(entity_list_mutex);

		auto& entities = main_view.scene->GetEntities();

		sorted_entities.clear();
		sorted_entities.reserve(entities.size());
		sorted_entities.insert(sorted_entities.end(), entities.begin(), entities.end());

		std::sort(sorted_entities.begin(), sorted_entities.end());
	}
}
