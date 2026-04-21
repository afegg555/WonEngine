#include "Editor.h"
#include "Input.h"
#include "ShaderCompiler.h"
#include "RHIResource.h"
#include "RHIShader.h"
#include "RHIPipeline.h"
#include "ShaderCompiler.h"
#include "FileSystem.h"
#include "Backlog.h"
#include "Profiler.h"
#include "SceneComponents.h"

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
				0x0100, 0x017F,	//0100 — 017F  	Latin Extended-A
				0x0180, 0x024F,	//0180 — 024F  	Latin Extended-B
				0,
			};

			float FONTUPSCALE = 1.0; //Font upscaling.
			float FontSize = 15.0f;
			ImGuiIO& io = ImGui::GetIO();
			io.Fonts->Clear();

			std::string font_file_path = font_folder_path + "/" + font_file_name;
			ImFont* custom_font = io.Fonts->AddFontFromFileTTF(font_file_path.c_str(), FontSize * FONTUPSCALE, NULL, &generic_ranges_everything[0]); //Set as default font.
			if (custom_font && merge_icon)
			{
				std::string font_icon_path = font_folder_path + "/MaterialIcons-Regular.ttf";
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
		AddImGuiFont(font_folder_path, "WantedSansStd-Regular.ttf");

		InitImGui();

		LoadDefaultPlugins();

		// camera entity
		{
			camera_entity = scene.CreateEntity();
			auto camera_transform = scene.AddComponent<ecs::TransformComponent>(camera_entity);
			if (camera_transform)
			{
				camera_transform->position = { 0.0f, 0.0f, -3.0f };
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
		imgui_font.reset();
		imgui_font_subresource = {};
		imgui_sampler.reset();
		scene = {};

		plugin_manager = {};

		Application::Shutdown();
	}

	void EditorApplication::Update(float dt)
	{
		Application::Update(dt);

		if (won::io::IsPressed(io::Button('R')))
		{
			rendering::ReloadShaderLibrary(device);
		}

		static CameraControllerAPI* controller_api = nullptr;
		static IPlugin* camera_controller = nullptr;
		if (!controller_api)
		{
			camera_controller = plugin_manager.GetPlugin(WON_IID_CAMERA_CONTROLLER).get();
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

		if (0 <= viewport_mouse_pos.x && viewport_mouse_pos.x <= main_viewport_size.x &&
			0 <= viewport_mouse_pos.y && viewport_mouse_pos.y <= main_viewport_size.y)
		{
			if (io::IsPressed(io::Button::MOUSE_BUTTON_LEFT) || io::IsPressed(io::Button::MOUSE_BUTTON_RIGHT)
				|| io::IsPressed(io::Button::MOUSE_BUTTON_MIDDLE))
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
				camera_state.cam_view = camera->forward;
				camera_state.cam_up = camera->up;

				const bool is_panmove = io::IsDown(io::Button::MOUSE_BUTTON_LEFT);
				const bool is_rotate = io::IsDown(io::Button::MOUSE_BUTTON_RIGHT);
				const bool is_orbit = io::IsDown(io::Button::MOUSE_BUTTON_MIDDLE);

				if (is_panmove)
				{
					controller_api->PanMove(camera_controller, mouse_delta, camera_state);
					transform->position = camera_state.cam_pos;
					transform->SetDirty();
				}

				if(is_rotate || is_orbit)
				{
					if (is_rotate)
					{
						controller_api->Rotate(camera_controller, mouse_delta, camera_state);
					}
					else
					{
						controller_api->Orbit(camera_controller, mouse_delta, camera_state);
					}

					XMVECTOR xeye = XMLoadFloat3(&camera_state.cam_pos);
					XMVECTOR xview = XMVector3Normalize(XMLoadFloat3(&camera_state.cam_view));
					XMVECTOR xup = XMVector3Normalize(XMLoadFloat3(&camera_state.cam_up));

					XMVECTOR xright = XMVector3Normalize(XMVector3Cross(xup, xview));
					XMVECTOR xup_reortho = XMVector3Normalize(XMVector3Cross(xview, xright));

					XMMATRIX cam_world;
					cam_world.r[0] = XMVectorSetW(xright, 0.0f);
					cam_world.r[1] = XMVectorSetW(xup_reortho, 0.0f);
					cam_world.r[2] = XMVectorSetW(xview, 0.0f);
					cam_world.r[3] = XMVectorSetW(xeye, 1.0f);

					XMVECTOR xrotation = XMQuaternionRotationMatrix(cam_world);
					XMStoreFloat3(&transform->position, xeye);
					XMStoreFloat4(&transform->rotation, xrotation);
					transform->SetDirty();
				}

				prev_mouse_pos = viewport_mouse_pos;
			}

			if (io::IsReleased(io::Button::MOUSE_BUTTON_LEFT) || io::IsReleased(io::Button::MOUSE_BUTTON_RIGHT)
				|| io::IsReleased(io::Button::MOUSE_BUTTON_MIDDLE))
			{
				pressed = false;
			}
		}
		
	}

	void EditorApplication::LoadDefaultPlugins()
	{
		if (!plugin_manager.LoadPlugin(WON_IID_ASSET_IMPORTER))
		{

		}
		if (!plugin_manager.LoadPlugin(WON_IID_CAMERA_CONTROLLER))
		{

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
				static bool use_wireframe = false;
				if (ImGui::Checkbox("WireFrame", &use_wireframe))
				{
					// TODO: WireFrame
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
			ecs::Entity delete_entity = INVALID_ENTITY;

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

						const bool is_selected = (selected_index == i);
						if (ImGui::Selectable(label.c_str(), is_selected))
						{
							selected_index = i;
						}

						if (ImGui::BeginPopupContextItem("EntityContextMenu"))
						{
							selected_index = i;
							picked_entity = id;

							if (ImGui::MenuItem("Delete Entity", nullptr, false, id != camera_entity))
							{
								delete_entity = id;
							}

							ImGui::EndPopup();
						}

						if (is_selected)
						{
							ImGui::SetItemDefaultFocus();
							picked_entity = id;
						}

						ImGui::PopID();
					}
				}
			}

			ImGui::EndChild();

			if (delete_entity != INVALID_ENTITY)
			{
				scene.DestroyEntity(delete_entity);
				UpdateEntityList();
				selected_index = -1;

				if (picked_entity == delete_entity)
				{
					picked_entity = INVALID_ENTITY;
				}
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
					ImGui::Text("NameComponent");
					char name_buf[256];
					std::snprintf(name_buf, sizeof(name_buf), "%s", name_comp->value.c_str());

					if (ImGui::InputText("Value", name_buf, sizeof(name_buf)))
					{
						name_comp->value = name_buf;
					}

					ImGui::Separator();
				}

				TransformComponent* transform_comp = main_view.scene->GetComponent<TransformComponent>(picked_entity);
				if (transform_comp)
				{
					ImGui::Text("TransformComponent");

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

					ImGui::Separator();
				}

				LightComponent* light_comp = main_view.scene->GetComponent<LightComponent>(picked_entity);
				if (light_comp)
				{
					ImGui::Text("LightComponent");
					
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

					ImGui::Separator();
				}

				CameraComponent* camera_comp = main_view.scene->GetComponent<CameraComponent>(picked_entity);
				if (camera_comp)
				{
					ImGui::Text("CameraComponent");

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

					ImGui::Separator();
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

		}
		ImGui::End();

		if (ImGui::Begin("Profiler", nullptr, ImGuiWindowFlags_NoScrollbar))
		{
			ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, -3));

			static bool profiler_enabled = false;
			if (ImGui::Checkbox("Enable Profiler", &profiler_enabled))
			{
				profiler::SetEnabled(profiler_enabled);
			}

			if (profiler_enabled)
			{
				ImGui::SameLine();
				ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Profiler Turned On! Performance may be reduced!");

				ImGui::SetCursorPos(ImGui::GetCursorPos() + ImVec2(0, 3));

				std::string performance, res_usage;
				profiler::GetProfileInfo(performance, res_usage);

				std::string profile = performance + "\n" + res_usage;
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

		Renderer::FrameContext& frame_context = renderer->GetFrameContext();
		Renderer::FrameUploadAllocation vb_allocation{}, ib_allocation{}, cb_allocation{};

		// Setup orthographic projection matrix into our constant buffer
		struct ImGuiConstants
		{
			float   mvp[4][4];
		};

		// Get memory for vertex and index buffers
		const uint64_t vbSize = sizeof(ImDrawVert) * drawData->TotalVtxCount;
		const uint64_t ibSize = sizeof(ImDrawIdx) * drawData->TotalIdxCount;
		const uint64_t cbSize = sizeof(ImGuiConstants);

		renderer->AllocateFrameUpload(frame_context, vbSize, 1, vb_allocation);
		renderer->AllocateFrameUpload(frame_context, ibSize, 1, ib_allocation);
		RHIBufferDesc cb_desc;
		cb_desc.bind_flags = RHIBindFlags::ConstantBuffer;
		auto cb_align = device->GetMinOffsetAlignment(cb_desc);
		renderer->AllocateFrameUpload(frame_context, cbSize, cb_align, cb_allocation);

		RHIViewport viewport;
		viewport.width = (float)fb_width;
		viewport.height = (float)fb_height;
		frame_context.command_list->SetViewport(viewport);
		frame_context.command_list->SetGraphicsPipeline(*imgui_pso);
		frame_context.command_list->SetSampler(RHIShaderStage::Pixel, 0, *imgui_sampler);

		// Copy and convert all vertices into a single contiguous buffer
		ImDrawVert* vertexCPUMem = reinterpret_cast<ImDrawVert*>(vb_allocation.mapped_data);
		ImDrawIdx* indexCPUMem = reinterpret_cast<ImDrawIdx*>(ib_allocation.mapped_data);
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
			memcpy(cb_allocation.mapped_data, mvp, sizeof(mvp));

			RHISubresourceHandle cb_subresource_handle{};

			RHISubresourceDesc subresource_desc{};
			subresource_desc.type = RHISubresourceType::ConstantBuffer;
			subresource_desc.buffer_offset = cb_allocation.buffer_offset;
			subresource_desc.buffer_size = cbSize;
			subresource_desc.buffer_stride = sizeof(ImGuiConstants);
			device->CreateSubresource(*frame_context.frame_upload_buffer, subresource_desc, &cb_subresource_handle);

			RHISubresourceBinding cb_binding;
			cb_binding.resource = frame_context.frame_upload_buffer.get();
			cb_binding.subresource = cb_subresource_handle;

			frame_context.command_list->SetVertexBuffer(*frame_context.frame_upload_buffer, sizeof(ImDrawVert), vb_allocation.buffer_offset, vbSize);
			frame_context.command_list->SetIndexBuffer(*frame_context.frame_upload_buffer, sizeof(ImDrawIdx), ib_allocation.buffer_offset, ibSize);
			frame_context.command_list->SetConstantBuffer(RHIShaderStage::Vertex, 0, cb_binding);
		}

		// Will project scissor/clipping rectangles into framebuffer space
		ImVec2 clip_off = drawData->DisplayPos;         // (0,0) unless using multi-viewports
		ImVec2 clip_scale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

		//passEncoder->SetSampler(0, Sampler::LinearWrap());

		// Render command lists
		int32_t vertexOffset = 0;
		uint32_t indexOffset = 0;
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
					frame_context.command_list->SetScissor(scissor);

					const RHIResource* texture = (const RHIResource*)drawCmd->GetTexID();
					RHISubresourceBinding binding;
					binding.resource = imgui_font.get();
					binding.subresource = imgui_font_subresource;
					frame_context.command_list->SetShaderResource(RHIShaderStage::Pixel, 0, binding);
					frame_context.command_list->DrawIndexed(drawCmd->ElemCount, 1, indexOffset + drawCmd->IdxOffset, vertexOffset + drawCmd->VtxOffset, 0);
				}
			}
			indexOffset += drawList->IdxBuffer.size();
			vertexOffset += drawList->VtxBuffer.size();
		}

		// Restore Scissor
		{
			RHIRect scissor;
			scissor.x = 0;
			scissor.y = 0;
			scissor.width = (int32_t)viewport.width;
			scissor.height = (int32_t)viewport.height;
			frame_context.command_list->SetScissor(scissor);
		}
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

	void EditorApplication::LoadSampleScene()
	{
		{
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

			//		mesh->CreateRenderData(device.get());
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
			auto asset_importer = plugin_manager.GetPlugin(WON_IID_ASSET_IMPORTER);
			AssetImporterAPI* api = (AssetImporterAPI*)asset_importer->QueryInterface(WON_IID_ASSET_IMPORTER, WON_VID_ASSET_IMPORTER);

			//std::string file_path = contents_root_dir + "/Models/glTF/Sponza/glTF/Sponza.gltf";
			std::string file_path = contents_root_dir + "/Models/Obj/Sphere/sphere.obj";
			ecs::Entity root_entity{};
			api->Import(asset_importer.get(), file_path.c_str(), &scene, device.get(), root_entity);

			{
				auto material_component = scene.GetComponent<ecs::MaterialComponent>(root_entity);
				for (uint32 i = 0; i < (uint32)material_component->GetMaterialSlotCount(); i++)
				{
					//auto& slot = material_component->GetMaterialSlot(i);
					//slot.shader_type = 
				}
				auto geometry_component = scene.GetComponent<ecs::GeometryComponent>(root_entity);
				geometry_component->SetCastShadow(true);
			}


			// light entity
			{
				ecs::Entity light_entity = scene.CreateEntity();
				auto transform = scene.AddComponent<ecs::TransformComponent>(light_entity);
				transform->RotateRollPitchYaw({ math::PI / 2.f, 0, 0 });
				//transform->Translate({ 0,0,-1 });
				auto light = scene.AddComponent<ecs::LightComponent>(light_entity);
				light->type = ecs::LightComponent::LightType::Directional;
				light->intensity = 100.f;
				//light->range = 20.f;
				//light->outer_cone_angle = math::PI / 3.f;
				//light->inner_cone_angle = math::PI / 6.f;

				auto name = scene.AddComponent<ecs::NameComponent>(light_entity);
				name->value = "Light";
			}

			// plane entity
			{
				ecs::Entity plane_entity = scene.CreateEntity();
				auto transform = scene.AddComponent<ecs::TransformComponent>(plane_entity);
				if (transform)
				{
					transform->Translate({ 0.f, -5.f, 0.f });
					transform->Scale({ 10.f, 10.f, 10.f });
				}
				auto geometry = scene.AddComponent<ecs::GeometryComponent>(plane_entity);
				if (geometry)
				{
					//geometry->SetCastShadow(true);

					auto mesh = std::make_shared<resource::Mesh>();
					mesh->positions = {
						{ 1.0f, 0.0f, 1.0f },
						{ -1.0f, 0.0f, 1.0f },
						{ 1.0f, 0.0f, -1.0f },
						{ -1.0f, 0.0f, -1.0f },
					};
					mesh->normals = {
						{ 0.0f, 1.0f, 0.f },
						{ 0.0f, 1.0f, 0.f },
						{ 0.0f, 1.0f, 0.f },
						{ 0.0f, 1.0f, 0.f },
					};

					mesh->indices = { 1, 0, 2, 1, 2, 3 };

					resource::Submesh submesh = {};
					submesh.first_index = 0;
					submesh.index_count = 6;
					submesh.first_vertex = 0;
					submesh.material_slot = 0;
					//submesh.local_bounds.min = { -0.5f, -0.5f, 0.0f };
					//submesh.local_bounds.max = { 0.5f, 0.5f, 0.0f };
					mesh->submeshes.push_back(submesh);

					geometry->mesh = mesh;
					geometry->local_bounds = submesh.local_bounds;

					mesh->CreateRenderData(device.get());
				}

				auto material = scene.AddComponent<ecs::MaterialComponent>(plane_entity);
				if (material)
				{
					auto& material_slot = material->AddMaterialSlot();
					material_slot.base_color = { 0.8f, 0.8f, 0.8f, 1.0f };
					material_slot.metallic = 1.0f;
					material_slot.roughness = 0.5f;
					material_slot.flags |= SHADER_MATERIAL_FLAG_RECEIVE_SHADOW;
				}

				auto name = scene.AddComponent<ecs::NameComponent>(plane_entity);
				name->value = "Plane";

			}
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


