#pragma once

#include "core\common.h"
#include "core\scene\world.h"
#include "renderGraph.h"
#include "enums.h"
#include "model.h"

namespace VulkanTest
{
	struct RendererPlugin;

	struct CameraComponent
	{
		F32 fov = MATH_PI / 3.0f;
		F32 nearZ = 0.1f;
		F32 farZ = 1000.0f;
		F32 width = 0.0f;
		F32 height = 0.0f;

		F32x3 eye = F32x3(0.0f, 0.0f, 0.0f);
		F32x3 at = F32x3(0.0f, 0.0f, 1.0f);
		F32x3 up = F32x3(0.0f, 1.0f, 0.0f);
		FMat4x4 view;
		FMat4x4 projection;
		FMat4x4 vp;

		// Component should preferably not contain any methods.
		void UpdateCamera();
	};

	class VULKAN_TEST_API RenderPassPlugin
	{
	public:
		virtual ~RenderPassPlugin() = default;

		virtual void Update(F32 dt) = 0;
		virtual void SetupRenderPasses(RenderGraph& graph) = 0;
		virtual void SetupDependencies(RenderGraph& graph) = 0;
		virtual void SetupResources(RenderGraph& graph) = 0;
	};

	struct RenderPassComponent
	{
		RenderPassPlugin* renderPassPlugin = nullptr;
	};
	
	class VULKAN_TEST_API RenderScene : public IScene
	{
	public:
		static UniquePtr<RenderScene> CreateScene(RendererPlugin& rendererPlugin, Engine& engine, World& world);

		virtual CameraComponent* GetMainCamera() = 0;

		// Scene methods
		virtual void UpdateVisibility(struct Visibility& vis) = 0;

		// Entity create methods
		virtual ECS::EntityID CreateMesh(const char* name) = 0;
	};
}