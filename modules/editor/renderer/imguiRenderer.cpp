#include "imguiRenderer.h"
#include "editor\editor.h"
#include "renderer\renderPath3D.h"
#include "renderer\renderer.h"
#include "core\jobsystem\jobsystem.h"
#include "core\filesystem\filesystem.h"
#include "core\utils\stream.h"

#include "glfw\include\GLFW\glfw3.h"
#include "imgui-docking\imgui.h"

namespace VulkanTest
{
namespace ImGuiRenderer
{
	App* app;
	ImFont* font;
	GPU::ImagePtr fontTexture;
	GPU::SamplerPtr sampler;

	struct ImGuiImplData {};

	ImGuiImplData* ImGuiImplGetBackendData()
	{
		return ImGui::GetCurrentContext() ? (ImGuiImplData*)ImGui::GetIO().BackendRendererUserData : nullptr;
	}

	void CreateDeviceObjects()
	{
		WSI& wsi = app->GetWSI();
		GPU::DeviceVulkan* device = wsi.GetDevice();

		// Get font texture data
		unsigned char* pixels;
		int width, height;
		ImFontAtlas* atlas = ImGui::GetIO().Fonts;
		atlas->GetTexDataAsRGBA32(&pixels, &width, &height);

		// Create font texture
		GPU::ImageCreateInfo imageInfo = GPU::ImageCreateInfo::ImmutableImage2D(width, height, VK_FORMAT_R8G8B8A8_UNORM);
		GPU::TextureFormatLayout formatLayout;
		formatLayout.SetTexture2D(VK_FORMAT_R8G8B8A8_UNORM, width, height);
		GPU::SubresourceData data = {};
		data.data = pixels;

		fontTexture = device->CreateImage(imageInfo, &data);
		device->SetName(*fontTexture, "ImGuiFontTexture");

		// Create sampler
		GPU::SamplerCreateInfo samplerInfo = {};
		samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		samplerInfo.minFilter = VK_FILTER_LINEAR;
		samplerInfo.magFilter = VK_FILTER_LINEAR;
		samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
		samplerInfo.anisotropyEnable = false;
		samplerInfo.compareEnable = false;

		sampler = device->RequestSampler(samplerInfo, false);
		device->SetName(*sampler, "ImGuiSampler");

		// Store our font texture
		ImGuiIO& io = ImGui::GetIO();
		io.Fonts->SetTexID((ImTextureID)&(*fontTexture));
	}

	ImFont* AddFontFromFile(App& app, const char* path)
	{
		Engine& engine = app.GetEngine();
		OutputMemoryStream mem;
		if (!engine.GetFileSystem().LoadContext(path, mem))
		{
			Logger::Error("Failed to load font %s", path);
			return nullptr;
		}

		const I32 dpi = Platform::GetDPI();
		F32 fontScale = dpi / 96.0f;
		F32 fontSize = 14.0f * fontScale * 1.25f;

		ImGuiIO& io = ImGui::GetIO();
		ImFontConfig cfg;
		CopyString(cfg.Name, path);
		cfg.FontDataOwnedByAtlas = false;
		ImFont* font = io.Fonts->AddFontFromMemoryTTF((void*)mem.Data(), (I32)mem.Size(), fontSize, &cfg);
		ASSERT(font != NULL);
		return nullptr;
	}

	static void UpdateImGuiMonitors() 
	{
		Platform::Monitor monitors[32];
		const U32 monitor_count = Platform::GetMonitors(Span(monitors));
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
		pio.Monitors.resize(0);
		for (U32 i = 0; i < monitor_count; ++i) 
		{
			const Platform::Monitor& m = monitors[i];
			ImGuiPlatformMonitor im;
			im.MainPos = ImVec2((F32)m.monitorRect.left, (F32)m.monitorRect.top);
			im.MainSize = ImVec2((F32)m.monitorRect.width, (F32)m.monitorRect.height);
			im.WorkPos = ImVec2((F32)m.workRect.left, (F32)m.workRect.top);
			im.WorkSize = ImVec2((F32)m.workRect.width, (F32)m.workRect.height);

			if (m.primary) {
				pio.Monitors.push_front(im);
			}
			else {
				pio.Monitors.push_back(im);
			}
		}
	}

	void InitPlatformIO(App& app)
	{
		ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
		static Editor::EditorApp* editor = (Editor::EditorApp*)(&app);
		pio.Platform_CreateWindow = [](ImGuiViewport* vp) 
		{
			Platform::WindowInitArgs args = {};
			args.flags = Platform::WindowInitArgs::NO_DECORATION | Platform::WindowInitArgs::NO_TASKBAR_ICON;
			ImGuiViewport* parent = ImGui::FindViewportByID(vp->ParentViewportId);
			args.parent = parent ? parent->PlatformHandle : Platform::INVALID_WINDOW;
			args.name = "child";
			vp->PlatformHandle = Platform::CreateCustomWindow(args);
			ASSERT(vp->PlatformHandle != Platform::INVALID_WINDOW);
			editor->AddWindow((Platform::WindowType)vp->PlatformHandle);
		};
		pio.Platform_DestroyWindow = [](ImGuiViewport* vp) 
		{
			Platform::WindowType w = (Platform::WindowType)vp->PlatformHandle;
			editor->DeferredDestroyWindow(w);
			vp->PlatformHandle = nullptr;
			vp->PlatformUserData = nullptr;
			editor->RemoveWindow(w);
		};
		pio.Platform_ShowWindow = [](ImGuiViewport* vp) {};
		pio.Platform_SetWindowPos = [](ImGuiViewport* vp, ImVec2 pos) {
			const Platform::WindowType h = (Platform::WindowType)vp->PlatformHandle;
			Platform::WindowRect r = Platform::GetWindowScreenRect(h);
			r.left = I32(pos.x);
			r.top = I32(pos.y);
			Platform::SetWindowScreenRect(h, r);
		};
		pio.Platform_GetWindowPos = [](ImGuiViewport* vp) -> ImVec2 {
			Platform::WindowType win = (Platform::WindowType)vp->PlatformHandle;
			const Platform::WindowRect r = Platform::GetClientBounds(win);
			const Platform::WindowPoint p = Platform::ToScreen(win, r.left, r.top);
			return { (F32)p.x, (F32)p.y };

		};
		pio.Platform_SetWindowSize = [](ImGuiViewport* vp, ImVec2 pos) {
			const Platform::WindowType h = (Platform::WindowType)vp->PlatformHandle;
			Platform::WindowRect r = Platform::GetWindowScreenRect(h);
			r.width = int(pos.x);
			r.height = int(pos.y);
			Platform::SetWindowScreenRect(h, r);
		};
		pio.Platform_GetWindowSize = [](ImGuiViewport* vp) -> ImVec2 {
			const Platform::WindowRect r = Platform::GetClientBounds((Platform::WindowType)vp->PlatformHandle);
			return { (F32)r.width, (F32)r.height };
		};
		pio.Platform_SetWindowTitle = [](ImGuiViewport* vp, const char* title) {};
		pio.Platform_GetWindowFocus = [](ImGuiViewport* vp) {
			return Platform::GetActiveWindow() == vp->PlatformHandle;
		};
		pio.Platform_SetWindowFocus = nullptr;

		ImGuiViewport* mvp = ImGui::GetMainViewport();
		mvp->PlatformHandle = app.GetPlatform().GetWindow();
		mvp->DpiScale = 1;
		mvp->PlatformUserData = (void*)1;

		UpdateImGuiMonitors();
	}

	void Initialize(App& app_)
	{
		Logger::Info("Initializing imgui...");
		app = &app_;

		IMGUI_CHECKVERSION();
		ImGui::CreateContext();

		// Setup context
		ImGuiIO& io = ImGui::GetIO();
		io.IniFilename = nullptr;
#ifdef CJING3D_PLATFORM_WIN32
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;
		io.BackendFlags = 
			ImGuiBackendFlags_PlatformHasViewports | 
			ImGuiBackendFlags_RendererHasViewports | 
			ImGuiBackendFlags_HasMouseCursors;
#else
		io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
		io.BackendFlags = ImGuiBackendFlags_HasMouseCursors;
#endif
		// Setup renderer backend
		ImGuiImplData* bd = IM_NEW(ImGuiImplData)();
		io.BackendRendererUserData = (void*)bd;
		io.BackendRendererName = "VulkanTest";

		// Setup platform
		InitPlatformIO(app_);

		// Add fonts
		font = AddFontFromFile(*app, "editor/fonts/notosans-regular.ttf");

		// Setup style
		ImGui::StyleColorsDark();
		ImGuiStyle& style = ImGui::GetStyle();
		style.FramePadding.y = 0;
		style.ItemSpacing.y = 2;
		style.ItemInnerSpacing.x = 2;
	}

	void Uninitialize()
	{
		sampler.reset();
		fontTexture.reset();

//#ifdef CJING3D_PLATFORM_WIN32
//		ImGui_ImplGlfw_Shutdown();
//#endif
		ImGui::DestroyContext();
		app = nullptr;
	}

	void BeginFrame()
	{
		auto backendData = ImGuiImplGetBackendData();
		ASSERT(backendData != nullptr);

		if (!fontTexture)
			CreateDeviceObjects();

		ImGuiIO& io = ImGui::GetIO();
		UpdateImGuiMonitors();

		Platform::WindowType window = app->GetPlatform().GetWindow();
		const Platform::WindowRect rect = Platform::GetClientBounds(window);
		if (rect.width > 0 && rect.height > 0) 
		{
			io.DisplaySize = ImVec2(float(rect.width), float(rect.height));
		}
		else if (io.DisplaySize.x <= 0) 
		{
			io.DisplaySize.x = 800;
			io.DisplaySize.y = 600;
		}
		io.DeltaTime = app->GetLastDeltaTime();

		ImGui::NewFrame();
		ImGui::PushFont(font);
	
		const ImGuiMouseCursor imguiCursor = ImGui::GetMouseCursor();
		switch (imguiCursor) {
			case ImGuiMouseCursor_Arrow: Platform::SetMouseCursorType(Platform::CursorType::DEFAULT); break;
			case ImGuiMouseCursor_ResizeNS: Platform::SetMouseCursorType(Platform::CursorType::SIZE_NS); break;
			case ImGuiMouseCursor_ResizeEW: Platform::SetMouseCursorType(Platform::CursorType::SIZE_WE); break;
			case ImGuiMouseCursor_ResizeNWSE: Platform::SetMouseCursorType(Platform::CursorType::SIZE_NWSE); break;
			case ImGuiMouseCursor_TextInput: Platform::SetMouseCursorType(Platform::CursorType::TEXT_INPUT); break;
			default: Platform::SetMouseCursorType(Platform::CursorType::DEFAULT); break;
		}
	}

	void EndFrame()
	{
		ImGui::PopFont();
		ImGui::Render();
		ImGui::UpdatePlatformWindows();
	}

	void RenderViewport(GPU::CommandList* cmd)
	{
		cmd->BeginEvent("ImGuiRender");
		ImGuiPlatformIO& platformIO = ImGui::GetPlatformIO();
		for (ImGuiViewport* vp : platformIO.Viewports)
		{
			ImDrawData* drawData = vp->DrawData;
			if (!drawData || drawData->TotalVtxCount == 0)
				return;

			int fbWidth = (int)(drawData->DisplaySize.x * drawData->FramebufferScale.x);
			int fbHeight = (int)(drawData->DisplaySize.y * drawData->FramebufferScale.y);
			if (fbWidth <= 0 || fbHeight <= 0)
				return;

			cmd->BeginEvent("ImGuiViewport");

			// Setup vertex buffer and index buffer
			const U64 vbSize = sizeof(ImDrawVert) * drawData->TotalVtxCount;
			const U64 ibSize = sizeof(ImDrawIdx) * drawData->TotalIdxCount;
			ImDrawVert* vertMem = static_cast<ImDrawVert*>(cmd->AllocateVertexBuffer(0, vbSize, sizeof(ImDrawVert), VK_VERTEX_INPUT_RATE_VERTEX));
			ImDrawIdx* indexMem = static_cast<ImDrawIdx*>(cmd->AllocateIndexBuffer(ibSize, VK_INDEX_TYPE_UINT16));

			for (int cmdListIdx = 0; cmdListIdx < drawData->CmdListsCount; cmdListIdx++)
			{
				const ImDrawList* drawList = drawData->CmdLists[cmdListIdx];
				memcpy(vertMem, &drawList->VtxBuffer[0], drawList->VtxBuffer.Size * sizeof(ImDrawVert));
				memcpy(indexMem, &drawList->IdxBuffer[0], drawList->IdxBuffer.Size * sizeof(ImDrawIdx));
				vertMem += drawList->VtxBuffer.Size;
				indexMem += drawList->IdxBuffer.Size;
			}

			// Setup mvp matrix
			F32 posX = drawData->DisplayPos.x;
			F32 posY = drawData->DisplayPos.y;
			F32 width = drawData->DisplaySize.x;
			F32 height = drawData->DisplaySize.y;
			struct ImGuiConstants
			{
				F32 mvp[2][4];
			};
			F32 mvp[2][4] =
			{
				{2.f / width, 0, -1.f - posX * 2.f / width, 0},
				{0, 2.f / height, -1.f - posY * 2.f / height, 0}
			};
			ImGuiConstants* constants = cmd->AllocateConstant<ImGuiConstants>(0, 0);
			memcpy(&constants->mvp, mvp, sizeof(mvp));

			cmd->SetProgram("editor/imGuiVS.hlsl", "editor/imGuiPS.hlsl");
			cmd->SetVertexAttribute(0, 0, VK_FORMAT_R32G32_SFLOAT, (U32)IM_OFFSETOF(ImDrawVert, pos));
			cmd->SetVertexAttribute(1, 0, VK_FORMAT_R32G32_SFLOAT, (U32)IM_OFFSETOF(ImDrawVert, uv));
			cmd->SetVertexAttribute(2, 0, VK_FORMAT_R8G8B8A8_UNORM, (U32)IM_OFFSETOF(ImDrawVert, col));

			// Set viewport
			VkViewport viewport = {};
			viewport.x = 0.0f;
			viewport.y = 0.0f;
			viewport.width = vp->Size.x;
			viewport.height = vp->Size.y;
			viewport.minDepth = 0.0f;
			viewport.maxDepth = 1.0f;
			cmd->SetViewport(viewport);

			// Set states
			cmd->SetSampler(0, 0, *sampler);
			cmd->SetDefaultTransparentState();
			cmd->SetBlendState(Renderer::GetBlendState(BlendStateType_Transparent));
			cmd->SetRasterizerState(Renderer::GetRasterizerState(RasterizerStateType_DoubleSided));
			cmd->SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);

			// Will project scissor/clipping rectangles into framebuffer space
			ImVec2 clipOff = drawData->DisplayPos;         // (0,0) unless using multi-viewports
			ImVec2 clipScale = drawData->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

			// Render command lists
			I32 vertexOffset = 0;
			U32 indexOffset = 0;
			for (U32 cmdListIdx = 0; cmdListIdx < (U32)drawData->CmdListsCount; ++cmdListIdx)
			{
				const ImDrawList* drawList = drawData->CmdLists[cmdListIdx];
				for (U32 cmdIndex = 0; cmdIndex < (U32)drawList->CmdBuffer.size(); ++cmdIndex)
				{
					const ImDrawCmd* drawCmd = &drawList->CmdBuffer[cmdIndex];
					ASSERT(!drawCmd->UserCallback);

					// Project scissor/clipping rectangles into framebuffer space
					ImVec2 clipMin(drawCmd->ClipRect.x - clipOff.x, drawCmd->ClipRect.y - clipOff.y);
					ImVec2 clipMax(drawCmd->ClipRect.z - clipOff.x, drawCmd->ClipRect.w - clipOff.y);
					if (clipMax.x < clipMin.x || clipMax.y < clipMin.y)
						continue;

					// Apply scissor/clipping rectangle
					VkRect2D scissor;
					scissor.offset.x = (I32)(clipMin.x);
					scissor.offset.y = (I32)(clipMin.y);
					scissor.extent.width = (I32)(clipMax.x - clipMin.x);
					scissor.extent.height = (I32)(clipMax.y - clipMin.y);
					cmd->SetScissor(scissor);

					const GPU::Image* texture = (const GPU::Image*)drawCmd->TextureId;
					cmd->SetTexture(0, 0, texture->GetImageView());
					cmd->DrawIndexed(drawCmd->ElemCount, indexOffset, vertexOffset);

					indexOffset += drawCmd->ElemCount;
				}
				vertexOffset += drawList->VtxBuffer.size();
			}

			cmd->EndEvent();
		}
		cmd->EndEvent();
	}

	void Render()
	{
		WSI& wsi = app->GetWSI();
		wsi.BeginFrame();

		auto device = wsi.GetDevice();

		GPU::CommandListPtr cmd = device->RequestCommandList(GPU::QUEUE_TYPE_GRAPHICS);
		cmd->BeginEvent("TriangleTest");
		GPU::RenderPassInfo rp = device->GetSwapchianRenderPassInfo(&wsi.GetSwapchain(), GPU::SwapchainRenderPassType::ColorOnly);
		cmd->BeginRenderPass(rp);
		{
			RenderViewport(cmd.get());
		}
		cmd->EndRenderPass();
		cmd->EndEvent();

		device->Submit(cmd);

		wsi.EndFrame();
	}
}
}