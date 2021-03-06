#include "renderPath3D.h"
#include "renderScene.h"
#include "renderer.h"
#include "imageUtil.h"
#include "shaderInterop_postprocess.h"

namespace VulkanTest
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

	void RenderPath3D::Update(float dt)
	{
		RenderScene* scene = GetScene();
		ASSERT(scene != nullptr);

		RenderPath2D::Update(dt);

		// Update main camera
		camera = scene->GetMainCamera();
		ASSERT(camera != nullptr);
		camera->width = (F32)currentBufferSize.x;
		camera->height = (F32)currentBufferSize.y;
		camera->UpdateCamera();

		// Culling for main camera
		visibility.Clear();
		visibility.scene = scene;
		visibility.camera = camera;
		visibility.flags = Visibility::ALLOW_EVERYTHING;
		scene->UpdateVisibility(visibility);

		// Update per frame data
		Renderer::UpdateFrameData(visibility, *scene, dt, frameCB);
	}

	void RenderPath3D::SetupPasses(RenderGraph& renderGraph)
	{
		GPU::DeviceVulkan* device = wsi->GetDevice();

		AttachmentInfo rtAttachmentInfo;
		rtAttachmentInfo.format = backbufferDim.format;
		rtAttachmentInfo.sizeX = (F32)backbufferDim.width;
		rtAttachmentInfo.sizeY = (F32)backbufferDim.height;
		
		AttachmentInfo depth;
		depth.format = device->GetDefaultDepthStencilFormat();
		depth.sizeX = (F32)backbufferDim.width;
		depth.sizeY = (F32)backbufferDim.height;

		///////////////////////////////////////////////////////////////////////////////////////////////
		// Depth prepass
		auto& preDepthPass = renderGraph.AddRenderPass("PreDepth", RenderGraphQueueFlag::Graphics);
		preDepthPass.WriteDepthStencil(SetDepthStencil("depth"), depth);
		preDepthPass.SetClearDepthStencilCallback(DefaultClearDepthFunc);
		preDepthPass.SetBuildCallback([&](GPU::CommandList& cmd) {

			GPU::Viewport viewport;
			viewport.width = (F32)backbufferDim.width;
			viewport.height = (F32)backbufferDim.height;
			cmd.SetViewport(viewport);
			Renderer::BindCameraCB(*camera, cmd);
			Renderer::DrawScene(cmd, visibility, RENDERPASS_PREPASS);
		});

		///////////////////////////////////////////////////////////////////////////////////////////////
		// Main opaque pass
		auto& opaquePass = renderGraph.AddRenderPass("Opaue", RenderGraphQueueFlag::Graphics);
		opaquePass.WriteColor(SetRenderResult3D("rtOpaque"), rtAttachmentInfo, "rtOpaque");
		opaquePass.SetClearColorCallback(DefaultClearColorFunc);
		opaquePass.ReadDepthStencil(GetDepthStencil());
		opaquePass.AddProxyOutput("opaque", VK_PIPELINE_STAGE_NONE_KHR);
		opaquePass.SetBuildCallback([&](GPU::CommandList& cmd) {

			GPU::Viewport viewport;
			viewport.width = (F32)backbufferDim.width;
			viewport.height = (F32)backbufferDim.height;
			cmd.SetViewport(viewport);

			Renderer::BindCameraCB(*camera, cmd);
			Renderer::DrawScene(cmd, visibility, RENDERPASS_MAIN);
		});

		///////////////////////////////////////////////////////////////////////////////////////////////
		// Transparents
		auto& transparentPass = renderGraph.AddRenderPass("Transparent", RenderGraphQueueFlag::Graphics);
		transparentPass.WriteColor(SetRenderResult3D("rtOpaque"), rtAttachmentInfo, "rtOpaque");
		transparentPass.ReadDepthStencil(GetDepthStencil());
		transparentPass.AddFakeResourceWriteAlias(GetDepthStencil(), "mainDepth");
		transparentPass.WriteDepthStencil(SetDepthStencil("mainDepth"), depth);
		transparentPass.AddProxyInput("opaque", VK_PIPELINE_STAGE_NONE_KHR);
		transparentPass.SetBuildCallback([&](GPU::CommandList& cmd) {

//			GPU::Viewport viewport;
//			viewport.width = (F32)backbufferDim.width;
//			viewport.height = (F32)backbufferDim.height;
//			cmd.SetViewport(viewport);
//
//			Renderer::BindCameraCB(*camera, cmd);
//			Renderer::BindCommonResources(cmd);
//;
//			cmd.SetProgram("screenVS.hlsl", "screenPS.hlsl");
//			cmd.SetDefaultTransparentState();
//			cmd.SetBlendState(Renderer::GetBlendState(BSTYPE_TRANSPARENT));
//			cmd.SetRasterizerState(Renderer::GetRasterizerState(RSTYPE_DOUBLE_SIDED));
//			cmd.SetDepthStencilState(Renderer::GetDepthStencilState(DSTYPE_DEFAULT));
//			cmd.Draw(3);
		});

		///////////////////////////////////////////////////////////////////////////////////////////////
		// Postprocess
		Renderer::SetupPostprocessBlurGaussian(renderGraph, GetRenderResult3D(), lastRenderPassRT, rtAttachmentInfo);

		RenderPath2D::SetupPasses(renderGraph);
	}

	void RenderPath3D::UpdateRenderData()
	{
		GPU::DeviceVulkan* device = wsi->GetDevice();
		auto cmd = device->RequestCommandList(GPU::QueueType::QUEUE_TYPE_GRAPHICS);
		Renderer::UpdateRenderData(visibility, frameCB, *cmd);
		device->Submit(cmd,  nullptr);
	}

	void RenderPath3D::SetupComposeDependency(RenderPass& composePass)
	{
		composePass.ReadTexture(GetRenderResult3D());
		RenderPath2D::SetupComposeDependency(composePass);
	}

	void RenderPath3D::Compose(RenderGraph& renderGraph, GPU::CommandList* cmd)
	{
		auto res = renderGraph.GetOrCreateTexture(GetRenderResult3D());
		auto& img = renderGraph.GetPhysicalTexture(res);

		ImageUtil::Params params = {};
		params.EnableFullScreen();
		params.blendFlag = BLENDMODE_OPAQUE;

		cmd->BeginEvent("Compose3D");
		ImageUtil::Draw(img.GetImage(), params, *cmd);
		cmd->EndEvent();

		RenderPath2D::Compose(renderGraph, cmd);
	}
}