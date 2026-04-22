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
				camera_transform->position = { 0.0f, 0.0f, -20.0f };
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

					if (ImGui::MenuItem("MaterialComponent"))
					{
						if (main_view.scene->GetComponent<MaterialComponent>(picked_entity) == nullptr)
						{
							main_view.scene->AddComponent<MaterialComponent>(picked_entity);
						}
					}

					ImGui::EndPopup();
				}

				ImGui::Separator();

				// TODO: use reflection system
				NameComponent* name_comp = main_view.scene->GetComponent<NameComponent>(picked_entity);
				if (name_comp)
				{
					ImGui::PushID("NameComponent");
					ImGui::Text("NameComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

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
						main_view.scene->RemoveComponent<NameComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				TransformComponent* transform_comp = main_view.scene->GetComponent<TransformComponent>(picked_entity);
				if (transform_comp)
				{
					ImGui::PushID("TransformComponent");
					ImGui::Text("TransformComponent");
					ImGui::SameLine();
					const bool can_remove_transform = picked_entity != camera_entity;
					if (!can_remove_transform)
					{
						ImGui::BeginDisabled();
					}
					bool remove_component = ImGui::Button("Remove");
					if (!can_remove_transform)
					{
						ImGui::EndDisabled();
					}

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
						main_view.scene->RemoveComponent<TransformComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				HierarchyComponent* hierarchy_comp = main_view.scene->GetComponent<HierarchyComponent>(picked_entity);
				if (hierarchy_comp)
				{
					ImGui::PushID("HierarchyComponent");
					ImGui::Text("HierarchyComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

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
						main_view.scene->RemoveComponent<HierarchyComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				LightComponent* light_comp = main_view.scene->GetComponent<LightComponent>(picked_entity);
				if (light_comp)
				{
					ImGui::PushID("LightComponent");
					ImGui::Text("LightComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");
					
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
						main_view.scene->RemoveComponent<LightComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				CameraComponent* camera_comp = main_view.scene->GetComponent<CameraComponent>(picked_entity);
				if (camera_comp)
				{
					ImGui::PushID("CameraComponent");
					ImGui::Text("CameraComponent");
					ImGui::SameLine();
					const bool can_remove_camera = picked_entity != camera_entity;
					if (!can_remove_camera)
					{
						ImGui::BeginDisabled();
					}
					bool remove_component = ImGui::Button("Remove");
					if (!can_remove_camera)
					{
						ImGui::EndDisabled();
					}

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
						main_view.scene->RemoveComponent<CameraComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				SkyComponent* sky_comp = main_view.scene->GetComponent<SkyComponent>(picked_entity);
				if (sky_comp)
				{
					ImGui::PushID("SkyComponent");
					ImGui::Text("SkyComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

					if (!remove_component)
					{
						bool is_active = sky_comp->IsActive();
						if (ImGui::Checkbox("Active", &is_active))
						{
							sky_comp->SetActive(is_active);
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
						main_view.scene->RemoveComponent<SkyComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				FogVolumeComponent* fog_volume_comp = main_view.scene->GetComponent<FogVolumeComponent>(picked_entity);
				if (fog_volume_comp)
				{
					ImGui::PushID("FogVolumeComponent");
					ImGui::Text("FogVolumeComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

					if (!remove_component)
					{
						bool is_active = fog_volume_comp->IsActive();
						if (ImGui::Checkbox("Active", &is_active))
						{
							fog_volume_comp->SetActive(is_active);
						}

						float half_extents[3] = { fog_volume_comp->half_extents.x, fog_volume_comp->half_extents.y, fog_volume_comp->half_extents.z };
						if (ImGui::InputFloat3("Half Extents", half_extents))
						{
							fog_volume_comp->half_extents = { half_extents[0], half_extents[1], half_extents[2] };
						}

						float color[3] = { fog_volume_comp->color.x, fog_volume_comp->color.y, fog_volume_comp->color.z };
						if (ImGui::InputFloat3("Color", color))
						{
							fog_volume_comp->color = { color[0], color[1], color[2] };
						}

						ImGui::DragFloat("Density", &fog_volume_comp->density, 0.001f, 0.0f, 10.0f);
						ImGui::DragFloat("Height Falloff", &fog_volume_comp->height_falloff, 0.001f, 0.0f, 100.0f);
						ImGui::DragFloat("Height Offset", &fog_volume_comp->height_offset, 0.01f, -100000.0f, 100000.0f);
						ImGui::DragFloat("Blend Distance", &fog_volume_comp->blend_distance, 0.01f, 0.0f, 100000.0f);
						ImGui::SliderFloat("Max Opacity", &fog_volume_comp->max_opacity, 0.0f, 1.0f);

						int priority = static_cast<int>(fog_volume_comp->priority);
						if (ImGui::InputInt("Priority", &priority))
						{
							fog_volume_comp->priority = static_cast<uint32>((std::max)(0, priority));
						}
					}
					else
					{
						main_view.scene->RemoveComponent<FogVolumeComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				EnvironmentLightingComponent* environment_lighting_comp = main_view.scene->GetComponent<EnvironmentLightingComponent>(picked_entity);
				if (environment_lighting_comp)
				{
					ImGui::PushID("EnvironmentLightingComponent");
					ImGui::Text("EnvironmentLightingComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

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
						main_view.scene->RemoveComponent<EnvironmentLightingComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				DDGIVolumeComponent* ddgi_volume_comp = main_view.scene->GetComponent<DDGIVolumeComponent>(picked_entity);
				if (ddgi_volume_comp)
				{
					ImGui::PushID("DDGIVolumeComponent");
					ImGui::Text("DDGIVolumeComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

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

						int rays_per_probe = static_cast<int>(ddgi_volume_comp->rays_per_probe);
						if (ImGui::InputInt("Rays Per Probe", &rays_per_probe))
						{
							ddgi_volume_comp->rays_per_probe = static_cast<uint32>((std::max)(1, rays_per_probe));
						}

						int irradiance_resolution = static_cast<int>(ddgi_volume_comp->irradiance_resolution);
						if (ImGui::InputInt("Irradiance Resolution", &irradiance_resolution))
						{
							ddgi_volume_comp->irradiance_resolution = static_cast<uint32>((std::max)(1, irradiance_resolution));
						}

						int visibility_resolution = static_cast<int>(ddgi_volume_comp->visibility_resolution);
						if (ImGui::InputInt("Visibility Resolution", &visibility_resolution))
						{
							ddgi_volume_comp->visibility_resolution = static_cast<uint32>((std::max)(1, visibility_resolution));
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
					}
					else
					{
						main_view.scene->RemoveComponent<DDGIVolumeComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				GeometryComponent* geometry_comp = main_view.scene->GetComponent<GeometryComponent>(picked_entity);
				if (geometry_comp)
				{
					ImGui::PushID("GeometryComponent");
					ImGui::Text("GeometryComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

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
						main_view.scene->RemoveComponent<GeometryComponent>(picked_entity);
					}

					ImGui::PopID();
					ImGui::Separator();
				}

				MaterialComponent* material_comp = main_view.scene->GetComponent<MaterialComponent>(picked_entity);
				if (material_comp)
				{
					ImGui::PushID("MaterialComponent");
					ImGui::Text("MaterialComponent");
					ImGui::SameLine();
					bool remove_component = ImGui::Button("Remove");

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
						main_view.scene->RemoveComponent<MaterialComponent>(picked_entity);
					}

					ImGui::PopID();
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
		Renderer::FrameUploadAllocation allocation{};
		auto gpu_range = profiler::ScopedRangeGPU("ImGui", *frame_context.command_list);

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
		renderer->AllocateFrameUpload(frame_context, totalSize, buffer_align, allocation);

		RHIViewport viewport;
		viewport.width = (float)fb_width;
		viewport.height = (float)fb_height;
		frame_context.command_list->SetViewport(viewport);
		frame_context.command_list->SetGraphicsPipeline(*imgui_pso);
		frame_context.command_list->SetSampler(RHIShaderStage::Pixel, 0, *imgui_sampler);

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
			device->CreateSubresource(*frame_context.frame_upload_buffer, subresource_desc, &cb_subresource_handle);

			RHISubresourceBinding cb_binding;
			cb_binding.resource = frame_context.frame_upload_buffer.get();
			cb_binding.subresource = cb_subresource_handle;

			frame_context.command_list->SetVertexBuffer(*frame_context.frame_upload_buffer, sizeof(ImDrawVert), vb_buffer_offset, vbSize);
			frame_context.command_list->SetIndexBuffer(*frame_context.frame_upload_buffer, sizeof(ImDrawIdx), ib_buffer_offset, ibSize);
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

			// environment entity
			{
				ecs::Entity env_entity = scene.CreateEntity();
				
				auto env = scene.AddComponent<ecs::SkyComponent>(env_entity);
				env->SetActive(true);
				scene.AddComponent<ecs::EnvironmentLightingComponent>(env_entity);

				auto name = scene.AddComponent<ecs::NameComponent>(env_entity);
				name->value = "Environment";
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


