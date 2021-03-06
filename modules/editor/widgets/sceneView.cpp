#include "sceneView.h"
#include "editor\editor.h"
#include "editor\widgets\gizmo.h"
#include "math\vMath_impl.hpp"
#include "renderer\renderScene.h"
#include "renderer\imageUtil.h"
#include "renderer\textureHelper.h"
#include "imgui-docking\imgui.h"

namespace VulkanTest
{
namespace Editor
{
	static bool DefaultClearColorFunc(U32 index, VkClearColorValue* value)
	{
		if (value != nullptr)
		{
			value->float32[0] = 0.0f;
			value->float32[1] = 0.0f;
			value->float32[2] = 0.0f;
			value->float32[3] = 0.0f;
		}
		return true;
	}

	static bool DefaultClearDepthFunc(VkClearDepthStencilValue* value)
	{
		if (value != nullptr)
		{
			value->depth = 0.0f;
			value->stencil = 0;
		}
		return true;
	}

	static const Color4 selectionOutlineColor = Color4(F32x4(1.0f, 0.6f, 0.0f, 1.0f));

	EditorRenderer::EditorRenderer(EditorApp& editor_) :
		RenderPath3D(),
		editor(editor_)
	{
	}

	void EditorRenderer::Update(F32 dt)
	{
		RenderScene* scene = GetScene();
		ASSERT(scene != nullptr);

		RenderPath2D::Update(dt);

		outlineTimer += dt;

		// Update main camera
		ASSERT(camera);
		camera->width = (F32)currentBufferSize.x;
		camera->height = (F32)currentBufferSize.y;
		camera->UpdateCamera();

		// Culling for main camera
		visibility.Clear();
		visibility.camera = camera;
		visibility.scene = scene;
		visibility.flags = Visibility::ALLOW_EVERYTHING;
		scene->UpdateVisibility(visibility);

		// Update per frame data
		Renderer::UpdateFrameData(visibility, *scene, dt, frameCB);
	}

	void EditorRenderer::Render()
	{
		UpdateRenderData();

		GPU::DeviceVulkan* device = wsi->GetDevice();
		auto& graph = GetRenderGraph();
		graph.SetupAttachments(*device, nullptr, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); // Used for sceneView
		Jobsystem::JobHandle handle;
		graph.Render(*device, handle);
		Jobsystem::Wait(&handle);
	}

	void EditorRenderer::SetupPasses(RenderGraph& renderGraph)
	{
		RenderPath3D::SetupPasses(renderGraph);

		GPU::DeviceVulkan* device = wsi->GetDevice();

		AttachmentInfo rtAttachmentInfo;
		rtAttachmentInfo.format = backbufferDim.format;
		rtAttachmentInfo.sizeX = (F32)backbufferDim.width;
		rtAttachmentInfo.sizeY = (F32)backbufferDim.height;

		AttachmentInfo outlineAttachmentInfo = rtAttachmentInfo;
		outlineAttachmentInfo.format = VK_FORMAT_R8_UNORM;

		// Outline
		auto& outline = renderGraph.AddRenderPass("EditorOutline", RenderGraphQueueFlag::Graphics);
		outline.WriteColor("rtOutline", outlineAttachmentInfo);
		outline.SetClearColorCallback(DefaultClearColorFunc);
		outline.ReadDepthStencil(GetDepthStencil());
		outline.SetBuildCallback([&](GPU::CommandList& cmd) {
	
			ImageUtil::Params params = {};
			params.EnableFullScreen();
			params.stencilMode = ImageUtil::STENCILMODE_EQUAL;
			params.stencilRef = 0x01;
			ImageUtil::Draw(TextureHelper::GetColor(Color4::White())->GetImage(), params, cmd);
		});

		// Main Editor
		auto& editorMain = renderGraph.AddRenderPass("EditorMain", RenderGraphQueueFlag::Graphics);
		editorMain.ReadTexture(GetRenderResult2D());
		editorMain.ReadTexture("rtOutline");
		editorMain.WriteColor(SetRenderResult2D("rtEditor"), rtAttachmentInfo);
		editorMain.SetClearColorCallback(DefaultClearColorFunc);
		editorMain.SetBuildCallback([&](GPU::CommandList& cmd) {

			GPU::Viewport viewport;
			viewport.width = (F32)backbufferDim.width;
			viewport.height = (F32)backbufferDim.height;
			cmd.SetViewport(viewport);

			// Show selection outline
			const auto& selected = editor.GetWorldEditor().GetSelectedEntities();
			if (!selected.empty())
			{
				auto& texOutline = renderGraph.GetPhysicalTexture(renderGraph.GetOrCreateTexture("rtOutline"));
				cmd.BeginEvent("Selection outline");
				float opacity = Lerp(0.4f, 1.0f, std::sin(outlineTimer * MATH_2PI * 0.6f) * 0.5f + 0.5f);
				Color4 color = selectionOutlineColor;
				color.SetA(color.GetA() * opacity);
				Renderer::PostprocessOutline(cmd, texOutline, 0.1f, 1.0f, color);
				cmd.EndEvent();
			}

			// Show gizmo
			cmd.BeginEvent("Gizmo");
			Gizmo::Config& config = editor.GetGizmoConfig();
			Gizmo::Draw(cmd, *GetCamera(), config);
			cmd.EndEvent();
		});
	}

	struct WorldViewImpl final : WorldView
	{
		enum class MouseMode
		{
			NONE,
			SELECT,
			NAVIGATE,
			COUNT
		};
		MouseMode mouseMode = MouseMode::NONE;

		SceneView& sceneView;
		EditorApp& editor;
		WorldEditor& worldEditor;
		CameraComponent camera;
		Transform transform;

		F32 deltaTime = 0.0f;
		bool isMouseDown[(int)Platform::MouseButton::COUNT] = {};
		bool isMouseClick[(int)Platform::MouseButton::COUNT] = {};
		F32x2 mousePos{0.f, 0.f};
		F32x2 mouseSensitivity{ 200.f, 200.f };

	public:
		WorldViewImpl(SceneView& view_, EditorApp& editor_) :
			sceneView(view_),
			editor(editor_),
			worldEditor(editor_.GetWorldEditor())
		{
			camera.up = F32x3(0.0f, 1.0f, 0.0f);
			camera.eye = F32x3(0.0f, 0.0f, 0.0f);
			camera.at = F32x3(0.0f, 0.0f, 1.0f);
			camera.width = -1.f;
			camera.height = -1.f;
		}

		virtual ~WorldViewImpl()
		{
		}

		void InputFrame() 
		{
			for (auto& i : isMouseClick) 
				i = false;
		}

		void OnMouseMove(int x, int y, int relx, int rely)
		{
			PROFILE_FUNCTION();
			mousePos = F32x2((F32)x, (F32)y);

			switch (mouseMode)
			{
			case WorldViewImpl::MouseMode::NAVIGATE:
			{
				const float yaw = Signum(relx) * (powf(fabsf((float)relx / mouseSensitivity.x), 1.2f));
				const float pitch = Signum(rely) * (powf(fabsf((float)rely / mouseSensitivity.y), 1.2f));
				transform.RotateRollPitchYaw(F32x3(pitch, yaw, 0.0f));
				transform.UpdateTransform();
				break;
			}
			default:
				break;
			}
		}

		void OnMouseDown(int x, int y, Platform::MouseButton button)
		{
			mousePos = F32x2((F32)x, (F32)y);
			isMouseClick[(int)button] = true;
			isMouseDown[(int)button] = true;

			if (button == Platform::MouseButton::RIGHT)
				mouseMode = MouseMode::NAVIGATE;

		}

		void OnMouseUp(int x, int y, Platform::MouseButton button)
		{
			isMouseDown[(int)button] = false;
			mouseMode = MouseMode::NONE;
		}

		void OnCameraMove(float forward, float right, float up, float speed)
		{
			F32x3 dir = F32x3(0.0f);
			dir.x += right;
			dir.y += up;
			dir.z += forward;
			dir *= speed;
			MATRIX rot = MatrixRotationQuaternion(LoadF32x4(transform.rotation));
			VECTOR move = Vector3TransformNormal(LoadF32x3(dir), rot);

			transform.Translate(StoreF32x3(move));
			transform.UpdateTransform();
		}

		void Update(F32 delta)
		{
			deltaTime = delta;
			transform.UpdateTransform();
			camera.UpdateTransform(transform);
		}

		bool IsMouseDown(Platform::MouseButton button) const override
		{
			return isMouseDown[(I32)button];
		}

		bool IsMouseClick(Platform::MouseButton button) const override
		{
			return isMouseClick[(I32)button];
		}

		F32x2 GetMousePos() const override
		{
			return mousePos;
		}

		const CameraComponent& GetCamera()const override
		{
			return camera;
		}
	};

	SceneView::SceneView(EditorApp& app_) :
		app(app_),
		worldEditor(app_.GetWorldEditor())
	{
		moveForwardAction.Init("Move forward", "moveForward");
		moveBackAction.Init("Move back", "moveBack");
		moveLeftAction.Init("Move left", "moveLeft");
		moveRightAction.Init("Move right", "moveRight");

		app.AddAction(&moveForwardAction);
		app.AddAction(&moveBackAction);
		app.AddAction(&moveLeftAction);
		app.AddAction(&moveRightAction);
	}

	SceneView::~SceneView()
	{
		app.RemoveAction(&moveForwardAction);
		app.RemoveAction(&moveBackAction);
		app.RemoveAction(&moveLeftAction);
		app.RemoveAction(&moveRightAction);

		CJING_SAFE_DELETE(worldView);
		editorRenderer.Reset();
	}

	void SceneView::Init()
	{
		worldView = CJING_NEW(WorldViewImpl)(*this, app);

		editorRenderer = CJING_MAKE_UNIQUE<EditorRenderer>(app);
		editorRenderer->SetWSI(&app.GetEngine().GetWSI());
		editorRenderer->DisableSwapchain();

		auto world = worldEditor.GetWorld();
		ASSERT(world);

		RenderScene* scene = dynamic_cast<RenderScene*>(world->GetScene("Renderer"));
		ASSERT(scene);
		editorRenderer->SetScene(scene);
	}

	void SceneView::Update(F32 dt)
	{
		PROFILE_FUNCTION();
		worldView->Update(dt);

		Manipulate();

		if (ImGui::IsAnyItemActive()) return;
		if (!isMouseCaptured) return;
		if (ImGui::GetIO().KeyCtrl) return;

		float speed = cameraSpeed * dt * 60.f;
		if (moveForwardAction.IsActive()) worldView->OnCameraMove(1.0f, 0, 0, speed);
		if (moveBackAction.IsActive()) worldView->OnCameraMove(-1.0f, 0, 0, speed);
		if (moveLeftAction.IsActive()) worldView->OnCameraMove(0.0f, -1.0f, 0, speed);
		if (moveRightAction.IsActive()) worldView->OnCameraMove(0.0f, 1.0f, 0, speed);
	}

	void SceneView::EndFrame()
	{
	}

	void SceneView::OnGUI()
	{
		PROFILE_FUNCTION();

		shouldRender = false;

		ImVec2 viewPos;
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
		if (ImGui::Begin("Scene View", nullptr, ImGuiWindowFlags_NoScrollWithMouse))
		{
			ImGui::Dummy(ImVec2(2, 2));
			OnToolbarGUI();
			worldView->InputFrame();

			const ImVec2 size = ImGui::GetContentRegionAvail();
			if (size.x <= 0 || size.y <= 0)
			{
				ImGui::End();
				ImGui::PopStyleVar();
				return;
			}

			editorRenderer->SetCamera(&worldView->camera);

			ImVec2 mouseScreenPos = ImGui::GetCursorScreenPos();
			screenPos.x = I32(mouseScreenPos.x);
			screenPos.y = I32(mouseScreenPos.y);
			screenSize.x = (I32)size.x;
			screenSize.y = (I32)size.y;

			U32x2 viewportSize = editorRenderer->GetViewportSize();
			if ((U32)size.x != viewportSize.x || (U32)size.y != viewportSize.y)
			{
				editorRenderer->SetViewportSize((U32)size.x, (U32)size.y);
				editorRenderer->ResizeBuffers();
			}
		
			auto& graph = editorRenderer->GetRenderGraph();
			auto& backRes = graph.GetOrCreateTexture("back");
			auto backTex = graph.TryGetPhysicalTexture(&backRes);
			if (backTex)
				ImGui::Image(graph.GetPhysicalTexture(backRes).GetImage(), size);

			// process drop target
			if (ImGui::BeginDragDropTarget()) 
			{
				if (auto* payload = ImGui::AcceptDragDropPayload("ResPath")) 
				{
					const ImVec2 mousePos = ImGui::GetMousePos();
					const ImVec2 dropPos = ImVec2(
						(mousePos.x - mouseScreenPos.y) / size.x,
						(mousePos.x - mouseScreenPos.y) / size.y );
					HandleDrop((const char*)payload->Data, dropPos.x, dropPos.y);
				}
				ImGui::EndDragDropTarget();
			}

			// Handle input events 
			HandleEvents();

			// Maybe should call it in SceneView::Update()
			editorRenderer->Update(worldView->deltaTime);

			shouldRender = true;
		}
		else
		{
			worldView->InputFrame();
		}

		if (isMouseCaptured && (Platform::GetFocusedWindow() != ImGui::GetWindowViewport()->PlatformHandle))
			CaptureMouse(false);

		ImGui::End();
		ImGui::PopStyleVar();
	}

	void SceneView::Render()
	{
		if (shouldRender)
			editorRenderer->Render();
	}

	const char* SceneView::GetName()
	{
		return "SceneView";
	}

	void SceneView::OnToolbarGUI()
	{
		static const char* actions_names[] = { 
			"SetTranslateGizmoMode",
			"SetRotateGizmoMode",
			"SetScaleGizmoMode",
		};

		auto pos = ImGui::GetCursorScreenPos();
		const float toolbarHeight = ImGui::GetTextLineHeightWithSpacing() + ImGui::GetStyle().FramePadding.y * 2;
		if (ImGuiEx::BeginToolbar("screenViewToolbar", pos, ImVec2(0, toolbarHeight)))
		{
			auto& cfg = app.GetGizmoConfig();
			ImGui::Checkbox("Transform", &cfg.enable);
			ImGui::SameLine();

			for (auto* action_name : actions_names)
			{
				auto* action = app.GetAction(action_name);
				action->ToolbarButton(app.GetBigIconFont());
			}
		}
		ImGui::SameLine();
		ImGui::Text("%.2fx%.2f", worldView->mousePos.x, worldView->mousePos.y);
		ImGuiEx::EndToolbar();
	}

	void SceneView::HandleEvents()
	{
		const bool handleInput = isMouseCaptured || (ImGui::IsItemHovered() && Platform::GetFocusedWindow() == ImGui::GetWindowViewport()->PlatformHandle);
		const auto& windowEvents = app.GetWindowEvents();
		for (auto& ent : windowEvents)
		{
			switch (ent.type)
			{
				case Platform::WindowEvent::Type::MOUSE_BUTTON:
				{
					I32x2 localMousePoint = GetLocalMousePoint();
					if (handleInput)
					{
						if (ent.mouseButton.button == Platform::MouseButton::RIGHT) 
						{
							ImGui::SetWindowFocus();
							CaptureMouse(ent.mouseButton.down);
						}

						if (ent.mouseButton.down) {
							worldView->OnMouseDown((int)localMousePoint.x, (int)localMousePoint.y, ent.mouseButton.button);
						}
						else {
							worldView->OnMouseUp((int)localMousePoint.x, (int)localMousePoint.y, ent.mouseButton.button);
						}
					}
					else if (!ent.mouseButton.down)
					{
					}
					break;
				}
				case Platform::WindowEvent::Type::MOUSE_MOVE:
				{
					if (handleInput)
					{
						I32x2 localMousePoint = GetLocalMousePoint();
						worldView->OnMouseMove((I32)localMousePoint.x, (I32)localMousePoint.y, (I32)ent.mouseMove.xrel, (I32)ent.mouseMove.yrel);
					}
					break;
				}
			}
		}
	}

	void SceneView::CaptureMouse(bool capture)
	{
		if (isMouseCaptured == capture)
			return;

		isMouseCaptured = capture;
		app.SetCursorCaptured(capture);
		Platform::SetMouseCursorVisible(!capture);

		if (capture)
		{
			Platform::GrabMouse((Platform::WindowType)ImGui::GetWindowViewport()->PlatformHandle);
			auto pos = Platform::GetMouseScreenPos();
			capturedMousePos.x = pos.x;
			capturedMousePos.y = pos.y;
		}
		else
		{
			Platform::GrabMouse(nullptr);
			Platform::SetMouseScreenPos(capturedMousePos.x, capturedMousePos.y);
		}
	}

	I32x2 SceneView::GetLocalMousePoint() const
	{
		const auto cp = Platform::GetMouseScreenPos();
		return I32x2(cp.x - screenPos.x, cp.y - screenPos.y);
	}

	void SceneView::HandleDrop(const char* path, float x, float y)
	{
		RenderScene* scene = dynamic_cast<RenderScene*>(worldEditor.GetWorld()->GetScene("Renderer"));
		if (scene == nullptr)
			return;

		char tmp[20];
		CopyString(Span(tmp), Path::GetExtension(Span(path, StringLength(path))));
		if (EqualIStrings(tmp, "obj"))
		{
			char name[64];
			CopyString(Span(name), Path::GetBaseName(path));
			scene->LoadModel(name, Path(path));
		}
	}

	void SceneView::Manipulate()
	{
		PROFILE_FUNCTION();
		Gizmo::Config& cfg = app.GetGizmoConfig();
		if (!cfg.enable) return;

		const auto& selected = worldEditor.GetSelectedEntities();
		if (selected.empty()) return;
		TransformComponent* comp = worldEditor.GetWorld()->GetComponent<TransformComponent>(selected[0]);
		if (comp == nullptr) return;
	
		Transform transform = comp->transform;
		const Transform oldPivot = transform;
		if (!Gizmo::Manipulate(selected[0], comp->transform, *worldView, cfg))
			return;

		const Transform newPivot = transform;
		switch (cfg.mode)
		{
		case Gizmo::Config::Mode::TRANSLATE:
		{
			F32x3 diff = newPivot.translation - oldPivot.translation;
			for (auto entity : selected)
			{
				TransformComponent* comp = worldEditor.GetWorld()->GetComponent<TransformComponent>(entity);
				if (comp != nullptr)
					comp->transform.Translate(diff);
			}
			break;
		}
		case Gizmo::Config::Mode::ROTATE:
		{
			break;
		}
		case Gizmo::Config::Mode::SCALE:
		{
			break;
		}
		default:
			ASSERT(false);
			break;
		}
	}
}
}