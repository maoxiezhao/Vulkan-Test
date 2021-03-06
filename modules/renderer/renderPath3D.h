#pragma once

#include "renderPath2D.h"
#include "culling.h"
#include "renderScene.h"

namespace VulkanTest
{
	class VULKAN_TEST_API RenderPath3D : public RenderPath2D
	{
	public:
		void Update(float dt) override;

		bool GetSceneUpdateEnabled()const {
			return sceneUpdateEnable;
		}

		void SetCamera(CameraComponent* camera_) {
			camera = camera_;
		}

		CameraComponent* GetCamera() {
			return camera;
		}

		String GetRenderResult3D()const {
			return lastRenderPassRT.c_str();
		}

		String SetRenderResult3D(const char* name)
		{
			lastRenderPassRT = name;
			return name;
		}

		virtual const String& GetDepthStencil()const {
			return lastDepthStencil;
		}

		String SetDepthStencil(const char* name) 
		{
			lastDepthStencil = name;
			return name;
		}

	protected:
		void SetupPasses(RenderGraph& renderGraph) override;
		void UpdateRenderData() override;
		void SetupComposeDependency(RenderPass& composePass) override;
		void Compose(RenderGraph& renderGraph, GPU::CommandList* cmd) override;

	protected:
		bool sceneUpdateEnable = true;

		Visibility visibility;
		CameraComponent* camera = nullptr;
		FrameCB frameCB = {};
		String lastRenderPassRT;
		String lastDepthStencil;
	};
}