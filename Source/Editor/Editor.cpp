#include "Editor.h"
#include "Input.h"
#include "ShaderCompiler.h"
#include "RHIResource.h"
#include "RHIShader.h"
#include "RHIPipeline.h"
#include "ShaderCompiler.h"
#include "FileSystem.h"
#include "Backlog.h"
#include "SceneComponents.h"

#define IMGUI_DEFINE_MATH_OPERATORS
#include "imgui-docking/imgui.h"
#include "imgui-docking/imgui_internal.h"
#ifdef _WIN32
#include "imgui-docking/imgui_impl_win32.h"
#endif
#include "IconsMaterialDesign.h"
#include "Themes.h"

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace won::editor
{
	using namespace resource;
	using namespace rendering;

	static RHIShader imgui_vs;
	static RHIShader imgui_ps;
	std::shared_ptr<RHIPipeline> imgui_pso;
	std::shared_ptr<RHIResource> imgui_font;
	RHISubresourceHandle imgui_font_subresource;
	std::shared_ptr<RHISampler> imgui_sampler;

	static String contents_root_dir;

	static float2 main_viewport_pos;
	static float2 main_viewport_size;

	ecs::Scene scene;
	ecs::Entity camera_entity;
	ecs::Entity image_entity;

#ifdef _WIN32
	HWND hWnd = NULL;
#endif

	namespace
	{
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
	}

	void EditorApplication::Initialize(const ApplicationDesc& desc)
	{
		Application::Initialize(desc);

		contents_root_dir = String(CONTENTS_ROOT_DIR) + "/";

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
		SetupVisualStudioStyle();

#ifdef _WIN32
		window->SetPlatformMessageHandler([](void* hwnd, uint32 message, Size wparam, Size lparam) -> bool
			{
				return ImGui_ImplWin32_WndProcHandler(static_cast<HWND>(hwnd), message, static_cast<WPARAM>(wparam), static_cast<LPARAM>(lparam)) != 0;
			});
		ImGui_ImplWin32_Init(window->GetNativeHandle());
#endif

		std::string font_folder_path = contents_root_dir + "Fonts";
		AddImGuiFont(font_folder_path, "WantedSansStd-Regular.ttf");

		ImGui_Impl_CreateDeviceObjects();

		// camera entity
		{
			camera_entity = scene.CreateEntity();
			auto* camera_transform = scene.AddComponent<ecs::TransformComponent>(camera_entity);
			if (camera_transform)
			{
				camera_transform->position = { 0.0f, 0.0f, -3.0f };
				camera_transform->SetDirty();
			}

			auto* camera = scene.AddComponent<ecs::CameraComponent>(camera_entity);
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
				camera->SetFOV_Y(math::PI / 2.0f);
				camera->SetOrtho(false);
				//camera->SetOrthoVerticalSize(4.f);
			}
		}
		main_view.scene = &scene;
		main_view.camera_entity = camera_entity;

		String file_path = io::GetWorkingDirectory() + "/../Contents/Images/env_comp.png";
		std::shared_ptr<resource::Image> image = resource::LoadImageFile(file_path, 4);
		if (!image || !image->IsValid())
		{
			backlog::Post(String("failed to load base color texture: ") + file_path, backlog::LogLevel::Warning);
			return;
		}

		RHITextureDesc texture_desc = {};
		texture_desc.width = static_cast<uint32>(image->width);
		texture_desc.height = static_cast<uint32>(image->height);
		texture_desc.depth = 1;
		texture_desc.mip_levels = 1;
		texture_desc.array_layers = 1;
		texture_desc.sample_count = 1;
		texture_desc.format = RHIFormat::R8G8B8A8Unorm;
		texture_desc.usage = RHIResourceUsage::Default;
		texture_desc.bind_flags = RHIBindFlags::ShaderResource;

		std::shared_ptr<RHIResource> texture_resource = device->CreateTexture(texture_desc, image->pixels.data(), image->pixels.size());
		if (!texture_resource)
		{
			backlog::Post("failed to create base color texture resource", backlog::LogLevel::Warning);
			return;
		}

		RHISubresourceDesc texture_srv_desc = {};
		texture_srv_desc.type = RHISubresourceType::ShaderResource;
		texture_srv_desc.first_slice = 0;
		texture_srv_desc.slice_count = 1;
		texture_srv_desc.first_mip = 0;
		texture_srv_desc.mip_count = 1;

		RHISubresourceHandle texture_srv = {};
		if (!device->CreateSubresource(*texture_resource, texture_srv_desc, &texture_srv))
		{
			backlog::Post("failed to create base color texture srv", backlog::LogLevel::Warning);
			return;
		}

		// image entity
		{
			image_entity = scene.CreateEntity();

			auto* transform = scene.AddComponent<ecs::TransformComponent>(image_entity);
			if (transform)
			{
				transform->position = { 0.0f, 0.0f, 0.0f };
				transform->Scale({ 2.f,2.f,2.f });
			}

			auto* geometry = scene.AddComponent<ecs::GeometryComponent>(image_entity);
			if (geometry)
			{
				auto mesh = std::make_shared<resource::Mesh>();
				mesh->positions = {
					{ 0.0f, 3.0f, 1.0f },
					{ 3.0f, 0.0f, 1.0f },
					{ -3.0f, 0.0f, 1.0f },
				};
				mesh->normals = {
					{ 0.0f, 0.0f, -1.f },
					{ 0.0f, 0.0f, -1.f },
					{ 0.0f, 0.0f, -1.f },
				};
				mesh->texcoords = {
					{ 0.5f, 0.0f },
					{ 1.0f, 1.0f },
					{ 0.0f, 1.0f },
				};

				mesh->indices = { 0, 1, 2 };

				resource::Submesh submesh = {};
				submesh.first_index = 0;
				//submesh.index_count = 3;
				submesh.index_count = 3;
				submesh.first_vertex = 0;
				submesh.material_slot = 0;
				submesh.local_bounds.min = { -0.5f, -0.5f, 0.0f };
				submesh.local_bounds.max = { 0.5f, 0.5f, 0.0f };
				mesh->submeshes.push_back(submesh);

				geometry->mesh = mesh;
				geometry->local_bounds = submesh.local_bounds;

				mesh->CreateRenderData(device);
			}

			auto* material = scene.AddComponent<ecs::MaterialComponent>(image_entity);
			if (material)
			{
				auto& material_slot = material->GetMaterialSlot(0);
				material_slot.base_color = { 1.0f, 1.0f, 1.0f, 1.0f };
				material_slot.metallic = 0.0f;
				material_slot.roughness = 1.0f;
				//material_slot.textures[0].name = "Test BaseColorMap";
				//material_slot.textures[0].texture = texture_resource;
				//material_slot.textures[0].res_handle = texture_srv;
			}
		}

		// light entity
		{
			ecs::Entity light_entity = scene.CreateEntity();
			auto* transform = scene.AddComponent<ecs::TransformComponent>(light_entity);
			//transform->RotateRollPitchYaw({ - math::PI / 12.f, 0, 0});
			transform->Translate({ 0,0,-1 });
			auto* light = scene.AddComponent<ecs::LightComponent>(light_entity);
			light->type = ecs::LightComponent::LightType::Point;
			light->intensity = 100.f;
			light->range = 20.f;
			light->outer_cone_angle = math::PI / 3.f;
			light->inner_cone_angle = math::PI / 6.f;
		}
	}

	void EditorApplication::Shutdown()
	{
		Application::Shutdown();
	}

	void EditorApplication::Update(float dt)
	{
		Application::Update(dt);

		if (won::io::IsPressed(io::Button('R')))
		{
			rendering::ReloadShaderLibrary(device);
		}
	}

	void EditorApplication::RenderUI()
	{
#ifdef _WIN32
		ImGui_ImplWin32_NewFrame();
#endif

		ImGui::NewFrame();
		ImGui::ShowDemoWindow();

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

	void EditorApplication::ImGui_Impl_CreateDeviceObjects()
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
}


