#include "renderer.h"
#include "shaderInterop.h"
#include "shaderInterop_renderer.h"
#include "shaderInterop_postprocess.h"
#include "gpu\vulkan\wsi.h"
#include "core\utils\profiler.h"
#include "core\resource\resourceManager.h"
#include "renderScene.h"
#include "renderPath3D.h"
#include "model.h"
#include "material.h"
#include "texture.h"
#include "textureHelper.h"
#include "imageUtil.h"

namespace VulkanTest
{

struct RendererPluginImpl : public RendererPlugin
{
public:
	RendererPluginImpl(Engine& engine_): engine(engine_) {}

	virtual ~RendererPluginImpl()
	{
		Renderer::Uninitialize();
	}

	void Initialize() override
	{
		Renderer::Initialize(engine);
	}

	GPU::DeviceVulkan* GetDevice() override
	{
		return engine.GetWSI().GetDevice();
	}

	RenderScene* GetScene() override
	{
		return scene;
	}

	const char* GetName() const override
	{
		return "Renderer";
	}

	void CreateScene(World& world) override 
	{
		UniquePtr<RenderScene> newScene = RenderScene::CreateScene(*this, engine, world);
		world.AddScene(newScene.Move());
	}

private:
	Engine& engine;
	RenderPath3D defaultPath;
	RenderScene* scene = nullptr;
};

namespace Renderer
{
	struct RenderBatch
	{
		U64 sortingKey;

		RenderBatch(ECS::EntityID mesh, ECS::EntityID obj, F32 distance)
		{
			ASSERT(mesh < 0x00FFFFFF);
			ASSERT(obj < 0x00FFFFFF);

			sortingKey = 0;
			sortingKey |= U64((U32)mesh & 0x00FFFFFF) << 40ull;
			sortingKey |= U64(ConvertFloatToHalf(distance) & 0xFFFF) << 24ull;
			sortingKey |= U64((U32)obj & 0x00FFFFFF) << 0ull;
		}

		inline float GetDistance() const
		{
			return ConvertHalfToFloat(HALF((sortingKey >> 24ull) & 0xFFFF));
		}

		inline ECS::EntityID GetMeshEntity() const
		{
			return ECS::EntityID((sortingKey >> 40ull) & 0x00FFFFFF);
		}

		inline ECS::EntityID GetInstanceEntity() const
		{
			return ECS::EntityID((sortingKey >> 0ull) & 0x00FFFFFF);
		}

		bool operator<(const RenderBatch& other) const
		{
			return sortingKey < other.sortingKey;
		}
	};

	struct RenderQueue
	{
		void SortOpaque()
		{
			std::sort(batches.begin(), batches.end(), std::less<RenderBatch>());
		}

		void Clear()
		{
			batches.clear();
		}

		void Add(ECS::EntityID mesh, ECS::EntityID obj, F32 distance)
		{
			batches.emplace(mesh, obj, distance);
		}

		bool Empty()const
		{
			return batches.empty();
		}

		size_t Size()const
		{
			return batches.size();
		}

		Array<RenderBatch> batches;
	};

	template <typename T>
	struct RenderResourceFactory : public ResourceFactory
	{
	protected:
		virtual Resource* CreateResource(const Path& path) override
		{
			return CJING_NEW(T)(path, *this);
		}

		virtual void DestroyResource(Resource* res) override
		{
			CJING_DELETE(res);
		}
	};
	RenderResourceFactory<Texture> textureFactory;
	RenderResourceFactory<Model> modelFactory;
	MaterialFactory materialFactory;

	GPU::BlendState stockBlendStates[BSTYPE_COUNT] = {};
	GPU::RasterizerState stockRasterizerState[RSTYPE_COUNT] = {};
	GPU::DepthStencilState depthStencilStates[DSTYPE_COUNT] = {};
	GPU::Shader* shaders[SHADERTYPE_COUNT] = {};
	GPU::BufferPtr frameBuffer;
	GPU::PipelineStateDesc objectPipelineStates
		[RENDERPASS_COUNT]
		[BLENDMODE_COUNT]
		[OBJECT_DOUBLESIDED_COUNT];

	RendererPlugin* rendererPlugin = nullptr;

	void InitStockStates()
	{
		// Blend states
		GPU::BlendState bd;
		bd.renderTarget[0].blendEnable = false;
		bd.renderTarget[0].srcBlend = VK_BLEND_FACTOR_SRC_ALPHA;
		bd.renderTarget[0].destBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		bd.renderTarget[0].blendOp = VK_BLEND_OP_MAX;
		bd.renderTarget[0].srcBlendAlpha = VK_BLEND_FACTOR_ONE;
		bd.renderTarget[0].destBlendAlpha = VK_BLEND_FACTOR_ZERO;
		bd.renderTarget[0].blendOpAlpha = VK_BLEND_OP_ADD;
		bd.renderTarget[0].renderTargetWriteMask = GPU::COLOR_WRITE_ENABLE_ALL;
		bd.alphaToCoverageEnable = false;
		bd.independentBlendEnable = false;
		stockBlendStates[BSTYPE_OPAQUE] = bd;

		bd.renderTarget[0].blendEnable = true;
		bd.renderTarget[0].srcBlend = VK_BLEND_FACTOR_SRC_ALPHA;
		bd.renderTarget[0].destBlend = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		bd.renderTarget[0].blendOp = VK_BLEND_OP_ADD;
		bd.renderTarget[0].srcBlendAlpha = VK_BLEND_FACTOR_ONE;
		bd.renderTarget[0].destBlendAlpha = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
		bd.renderTarget[0].blendOpAlpha = VK_BLEND_OP_ADD;
		bd.renderTarget[0].renderTargetWriteMask = GPU::COLOR_WRITE_ENABLE_ALL;
		stockBlendStates[BSTYPE_TRANSPARENT] = bd;

		bd.renderTarget[0].blendEnable = true;
		bd.renderTarget[0].srcBlend = VK_BLEND_FACTOR_ONE;
		bd.renderTarget[0].destBlend = VK_BLEND_FACTOR_SRC_ALPHA;
		bd.renderTarget[0].blendOp = VK_BLEND_OP_ADD;
		bd.renderTarget[0].srcBlendAlpha = VK_BLEND_FACTOR_ONE;
		bd.renderTarget[0].destBlendAlpha = VK_BLEND_FACTOR_SRC_ALPHA;
		bd.renderTarget[0].blendOpAlpha = VK_BLEND_OP_ADD;
		bd.renderTarget[0].renderTargetWriteMask = GPU::COLOR_WRITE_ENABLE_ALL;
		stockBlendStates[BSTYPE_PREMULTIPLIED] = bd;

		// Rasterizer states
		GPU::RasterizerState rs;
		rs.fillMode = GPU::FILL_SOLID;
		rs.cullMode = VK_CULL_MODE_BACK_BIT;
		rs.frontCounterClockwise = true;
		rs.depthBias = 0;
		rs.depthBiasClamp = 0;
		rs.slopeScaledDepthBias = 0;
		rs.depthClipEnable = false;
		rs.multisampleEnable = false;
		rs.antialiasedLineEnable = false;
		rs.conservativeRasterizationEnable = false;
		stockRasterizerState[RSTYPE_FRONT] = rs;

		rs.cullMode = VK_CULL_MODE_FRONT_BIT;
		stockRasterizerState[RSTYPE_BACK] = rs;

		rs.fillMode = GPU::FILL_SOLID;
		rs.cullMode = VK_CULL_MODE_NONE;
		rs.frontCounterClockwise = false;
		rs.depthBias = 0;
		rs.depthBiasClamp = 0;
		rs.slopeScaledDepthBias = 0;
		rs.depthClipEnable = false;
		rs.multisampleEnable = false;
		rs.antialiasedLineEnable = false;
		rs.conservativeRasterizationEnable = false;
		stockRasterizerState[RSTYPE_DOUBLE_SIDED] = rs;

		// DepthStencilStates
		GPU::DepthStencilState dsd;
		dsd.depthEnable = true;
		dsd.depthWriteMask = GPU::DEPTH_WRITE_MASK_ALL;
		dsd.depthFunc = VK_COMPARE_OP_GREATER;

		dsd.stencilEnable = true;
		dsd.stencilReadMask = 0;
		dsd.stencilWriteMask = 0xFF;
		dsd.frontFace.stencilFunc = VK_COMPARE_OP_ALWAYS;
		dsd.frontFace.stencilPassOp = VK_STENCIL_OP_REPLACE;
		dsd.frontFace.stencilFailOp = VK_STENCIL_OP_KEEP;
		dsd.frontFace.stencilDepthFailOp = VK_STENCIL_OP_KEEP;
		dsd.backFace.stencilFunc = VK_COMPARE_OP_ALWAYS;
		dsd.backFace.stencilPassOp = VK_STENCIL_OP_REPLACE;
		dsd.backFace.stencilFailOp = VK_STENCIL_OP_KEEP;
		dsd.backFace.stencilDepthFailOp = VK_STENCIL_OP_KEEP;
		depthStencilStates[DSTYPE_DEFAULT] = dsd;

		dsd.depthEnable = true;
		dsd.depthWriteMask = GPU::DEPTH_WRITE_MASK_ZERO;
		dsd.depthFunc = VK_COMPARE_OP_EQUAL;
		depthStencilStates[DSTYPE_READEQUAL] = dsd;

		dsd.depthEnable = false;
		dsd.stencilEnable = false;
		depthStencilStates[DSTYPE_DISABLED] = dsd;
	}

	GPU::Shader* PreloadShader(GPU::ShaderStage stage, const char* path, const GPU::ShaderVariantMap& defines)
	{
		GPU::DeviceVulkan& device = *GetDevice();
		return device.GetShaderManager().LoadShader(stage, path, defines);
	}

	void LoadShaders()
	{
		// TODO: Replace Shaders with Shader resource

		shaders[SHADERTYPE_VS_OBJECT] = PreloadShader(GPU::ShaderStage::VS, "objectVS.hlsl", {"OBJECTSHADER_LAYOUT_COMMON"});
		shaders[SHADERTYPE_VS_PREPASS] = PreloadShader(GPU::ShaderStage::VS, "objectVS.hlsl", {"OBJECTSHADER_LAYOUT_PREPASS"});
		shaders[SHADERTYPE_VS_VERTEXCOLOR] = PreloadShader(GPU::ShaderStage::VS, "vertexColorVS.hlsl");
		shaders[SHADERTYPE_VS_POSTPROCESS] = PreloadShader(GPU::ShaderStage::VS, "postprocessVS.hlsl");

		shaders[SHADERTYPE_CS_POSTPROCESS_BLUR_GAUSSIAN] = PreloadShader(GPU::ShaderStage::CS, "blurGaussianCS.hlsl");

		shaders[SHADERTYPE_PS_OBJECT] = PreloadShader(GPU::ShaderStage::PS, "objectPS.hlsl", { "OBJECTSHADER_LAYOUT_COMMON" });
		shaders[SHADERTYPE_PS_PREPASS] = PreloadShader(GPU::ShaderStage::PS, "objectPS.hlsl", { "OBJECTSHADER_LAYOUT_PREPASS" });
		shaders[SHADERTYPE_PS_VERTEXCOLOR] = PreloadShader(GPU::ShaderStage::PS, "vertexColorPS.hlsl");
		shaders[SHADERTYPE_PS_POSTPROCESS_OUTLINE] = PreloadShader(GPU::ShaderStage::PS, "outlinePS.hlsl");
	}

	ShaderType GetVSType(RENDERPASS renderPass)
	{
		switch (renderPass)
		{
		case RENDERPASS_MAIN:
			return SHADERTYPE_VS_OBJECT;
			break;
		case RENDERPASS_PREPASS:
			return SHADERTYPE_VS_PREPASS;
			break;
		default:
			return SHADERTYPE_COUNT;
		}
	}

	ShaderType GetPSType(RENDERPASS renderPass)
	{
		switch (renderPass)
		{
		case RENDERPASS_MAIN:
			return SHADERTYPE_PS_OBJECT;
			break;
		case RENDERPASS_PREPASS:
			return SHADERTYPE_PS_PREPASS;
			break;
		default:
			return SHADERTYPE_COUNT;
		}
	}

	void LoadPipelineStates()
	{
		for (I32 renderPass = 0; renderPass < RENDERPASS_COUNT; ++renderPass)
		{
			for (I32 blendMode = 0; blendMode < BLENDMODE_COUNT; ++blendMode)
			{
				for (I32 doublesided = 0; doublesided < OBJECT_DOUBLESIDED_COUNT; ++doublesided)
				{
					GPU::PipelineStateDesc pipeline = {};
					memset(&pipeline, 0, sizeof(pipeline));

					// Shaders
					const bool transparency = blendMode != BLENDMODE_OPAQUE;
					ShaderType vsType = GetVSType((RENDERPASS)renderPass);
					ShaderType psType = GetPSType((RENDERPASS)renderPass);

					pipeline.shaders[(I32)GPU::ShaderStage::VS] = vsType < SHADERTYPE_COUNT ? GetShader(vsType) : nullptr;
					pipeline.shaders[(I32)GPU::ShaderStage::PS] = psType < SHADERTYPE_COUNT ? GetShader(psType) : nullptr;
					
					// Blend states
					switch (blendMode)
					{
					case BLENDMODE_OPAQUE:
						pipeline.blendState = GetBlendState(BlendStateTypes::BSTYPE_OPAQUE);
						break;
					case BLENDMODE_ALPHA:
						pipeline.blendState = GetBlendState(BlendStateTypes::BSTYPE_TRANSPARENT);
						break;
					case BLENDMODE_PREMULTIPLIED:
						pipeline.blendState = GetBlendState(BlendStateTypes::BSTYPE_PREMULTIPLIED);
						break;
					default:
						ASSERT(false);
						break;
					}

					// DepthStencilStates
					switch (renderPass)
					{
					case RENDERPASS_MAIN:
						pipeline.depthStencilState = GetDepthStencilState(DepthStencilStateType::DSTYPE_READEQUAL);
						break;
					default:
						pipeline.depthStencilState = GetDepthStencilState(DepthStencilStateType::DSTYPE_DEFAULT);
						break;
					}

					// RasterizerStates
					switch (doublesided)
					{
					case OBJECT_DOUBLESIDED_FRONTSIDE:
						pipeline.rasterizerState = GetRasterizerState(RasterizerStateTypes::RSTYPE_FRONT);
						break;
					case OBJECT_DOUBLESIDED_ENABLED:
						pipeline.rasterizerState = GetRasterizerState(RasterizerStateTypes::RSTYPE_DOUBLE_SIDED);
						break;
					case OBJECT_DOUBLESIDED_BACKSIDE:
						pipeline.rasterizerState = GetRasterizerState(RasterizerStateTypes::RSTYPE_BACK);
						break;
					default:
						ASSERT(false);
						break;
					}
	
					objectPipelineStates[renderPass][blendMode][doublesided] = pipeline;
				}
			}
		}
	}

	void Renderer::Initialize(Engine& engine)
	{
		Logger::Info("Render initialized");

		// Load built-in states
		InitStockStates();
		LoadShaders();
		LoadPipelineStates();

		// Create built-in constant buffers
		auto device = GetDevice();
		GPU::BufferCreateInfo info = {};
		info.domain = GPU::BufferDomain::Device;
		info.size = sizeof(FrameCB);
		info.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		frameBuffer = device->CreateBuffer(info, nullptr);
		device->SetName(*frameBuffer, "FrameBuffer");

		// Initialize resource factories
		ResourceManager& resManager = engine.GetResourceManager();
		textureFactory.Initialize(Texture::ResType, resManager);
		modelFactory.Initialize(Model::ResType, resManager);
		materialFactory.Initialize(Material::ResType, resManager);

		// Initialize image util
		ImageUtil::Initialize();

		// Initialize texture helper
		TextureHelper::Initialize(&resManager);
	}

	void Renderer::Uninitialize()
	{
		TextureHelper::Uninitialize();

		frameBuffer.reset();

		// Uninitialize resource factories
		materialFactory.Uninitialize();
		modelFactory.Uninitialize();
		textureFactory.Uninitialize();

		rendererPlugin = nullptr;
		Logger::Info("Render uninitialized");
	}

	GPU::DeviceVulkan* GetDevice()
	{
		ASSERT(rendererPlugin != nullptr);
		return rendererPlugin->GetDevice();
	}

	const GPU::BlendState& GetBlendState(BlendStateTypes type)
	{
		return stockBlendStates[type];
	}
	
	const GPU::RasterizerState& GetRasterizerState(RasterizerStateTypes type)
	{
		return stockRasterizerState[type];
	}

	const GPU::DepthStencilState& GetDepthStencilState(DepthStencilStateType type)
	{
		return depthStencilStates[type];
	}

	const GPU::Shader* GetShader(ShaderType type)
	{
		return shaders[type];
	}

	const GPU::PipelineStateDesc& GetObjectPipelineState(RENDERPASS renderPass, BlendMode blendMode, ObjectDoubleSided doublesided)
	{
		return objectPipelineStates[renderPass][blendMode][doublesided];
	}

	void DrawDebugObjects(const RenderScene& scene, const CameraComponent& camera, GPU::CommandList& cmd)
	{
	}

	void DrawBox(const FMat4x4& boxMatrix, const F32x4& color)
	{
	}

	void UpdateFrameData(const Visibility& visible, RenderScene& scene, F32 delta, FrameCB& frameCB)
	{
		ASSERT(visible.scene);
		frameCB.scene = visible.scene->GetShaderScene();
	}

	void UpdateRenderData(const Visibility& visible, const FrameCB& frameCB, GPU::CommandList& cmd)
	{
		ASSERT(visible.scene);
		cmd.BeginEvent("UpdateRenderData");

		// Update frame constbuffer
		cmd.UpdateBuffer(frameBuffer.get(), &frameCB, sizeof(frameCB));
		cmd.BufferBarrier(*frameBuffer,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_ACCESS_TRANSFER_WRITE_BIT,
			VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
			VK_ACCESS_UNIFORM_READ_BIT);

		visible.scene->UpdateRenderData(cmd);

		cmd.EndEvent();
	}

	void BindCameraCB(const CameraComponent& camera, GPU::CommandList& cmd)
	{
		CameraCB cb;
		cb.viewProjection = camera.viewProjection;
		cmd.BindConstant(cb, 0, CBSLOT_RENDERER_CAMERA);
	}

	void BindCommonResources(GPU::CommandList& cmd)
	{
		auto heap = cmd.GetDevice().GetBindlessDescriptorHeap(GPU::BindlessReosurceType::StorageBuffer);
		if (heap != nullptr)
			cmd.SetBindless(1, heap->GetDescriptorSet());

		cmd.BindConstantBuffer(frameBuffer, 0, CBSLOT_RENDERER_FRAME, 0, sizeof(FrameCB));
	}

	Ray GetPickRay(const F32x2& screenPos, const CameraComponent& camera)
	{
		F32 w = camera.width;
		F32 h = camera.height;

		MATRIX V = LoadFMat4x4(camera.view);
		MATRIX P = LoadFMat4x4(camera.projection);
		MATRIX W = MatrixIdentity();
		VECTOR lineStart = Vector3Unproject(XMVectorSet(screenPos.x, screenPos.y, 1, 1), 0.0f, 0.0f, w, h, 0.0f, 1.0f, P, V, W);
		VECTOR lineEnd = Vector3Unproject(XMVectorSet(screenPos.x, screenPos.y, 0, 1), 0.0f, 0.0f, w, h, 0.0f, 1.0f, P, V, W);
		VECTOR rayDirection = Vector3Normalize(VectorSubtract(lineEnd, lineStart));
		return Ray(StoreF32x3(lineStart), StoreF32x3(rayDirection));
	}

	void DrawMeshes(GPU::CommandList& cmd, const RenderQueue& queue, const Visibility& vis, RENDERPASS renderPass, U32 renderFlags)
	{
		if (queue.Empty())
			return;

		RenderScene* scene = vis.scene;
		cmd.BeginEvent("DrawMeshes");

		const size_t allocSize = queue.Size() * sizeof(ShaderMeshInstancePointer);
		GPU::BufferBlockAllocation allocation = cmd.AllocateStorageBuffer(allocSize);

		struct InstancedBatch
		{
			ECS::EntityID meshID = ECS::INVALID_ENTITY;
			uint32_t instanceCount = 0;
			uint32_t dataOffset = 0;
			U8 stencilRef = 0;
		} instancedBatch = {};

		auto FlushBatch = [&]()
		{
			if (instancedBatch.instanceCount <= 0)
				return;

			MeshComponent* meshCmp = scene->GetComponent<MeshComponent>(instancedBatch.meshID);
			if (meshCmp == nullptr ||
				meshCmp->mesh == nullptr)
				return;

			Mesh& mesh = *meshCmp->mesh;
			cmd.BindIndexBuffer(mesh.generalBuffer, mesh.ib.offset, VK_INDEX_TYPE_UINT32);

			for (U32 subsetIndex = 0; subsetIndex < mesh.subsets.size(); subsetIndex++)
			{
				auto& subset = mesh.subsets[subsetIndex];
				if (subset.indexCount <= 0)
					continue;

				MaterialComponent* material = scene->GetComponent<MaterialComponent>(subset.materialID);
				if (!material || !material->material)
					continue;

				BlendMode blendMode = material->material->GetBlendMode();
				ObjectDoubleSided doubleSided = material->material->IsDoubleSided() ? OBJECT_DOUBLESIDED_ENABLED : OBJECT_DOUBLESIDED_FRONTSIDE;
				cmd.SetPipelineState(GetObjectPipelineState(renderPass, blendMode, doubleSided));

				// StencilRef
				U8 stencilRef = instancedBatch.stencilRef;
				cmd.SetStencilRef(stencilRef, GPU::STENCIL_FACE_FRONT_AND_BACK);

				// PushConstants
				ObjectPushConstants push;
				push.geometryIndex = meshCmp->geometryOffset + subsetIndex;
				push.materialIndex = material != nullptr ? material->materialIndex : 0;
				push.instance = allocation.bindless ? allocation.bindless->GetIndex() : -1;	// Pointer to ShaderInstancePointers
				push.instanceOffset = (U32)instancedBatch.dataOffset;

				cmd.SetPrimitiveTopology(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST);
				cmd.PushConstants(&push, 0, sizeof(push));
				cmd.DrawIndexedInstanced(subset.indexCount, instancedBatch.instanceCount, subset.indexOffset, 0, 0);
			}
		};

		U32 instanceCount = 0;
		for (auto& batch : queue.batches)
		{
			const ECS::EntityID objID = batch.GetInstanceEntity();
			const ECS::EntityID meshID = batch.GetMeshEntity();

			ObjectComponent* obj = scene->GetComponent<ObjectComponent>(objID);
			if (meshID != instancedBatch.meshID ||
				obj->stencilRef != instancedBatch.stencilRef)
			{
				FlushBatch();

				instancedBatch = {};
				instancedBatch.meshID = meshID;
				instancedBatch.dataOffset = allocation.offset + instanceCount * sizeof(ShaderMeshInstancePointer);
				instancedBatch.stencilRef = obj->stencilRef;
			}

			ShaderMeshInstancePointer data;
			data.instanceIndex = obj->index;
			memcpy((ShaderMeshInstancePointer*)allocation.data + instanceCount, &data, sizeof(ShaderMeshInstancePointer));

			instancedBatch.instanceCount++;
			instanceCount++;
		}

		FlushBatch();
		cmd.EndEvent();
	}

	void DrawScene(GPU::CommandList& cmd, const Visibility& vis, RENDERPASS pass)
	{
		RenderScene* scene = vis.scene;
		if (!scene)
			return;

		cmd.BeginEvent("DrawScene");

		BindCommonResources(cmd);

#if 0
		static thread_local RenderQueue queue;
#else
		RenderQueue queue;
#endif
		queue.Clear();
		for (auto objectID : vis.objects)
		{
			ObjectComponent* obj = scene->GetComponent<ObjectComponent>(objectID);
			if (obj == nullptr || obj->mesh == ECS::INVALID_ENTITY)
				continue;

			const F32 distance = Distance(vis.camera->eye, obj->center);
			queue.Add(obj->mesh, objectID, distance);
		}

		if (!queue.Empty())
		{
			queue.SortOpaque();
			DrawMeshes(cmd, queue, vis, pass, 0);
		}

		cmd.EndEvent();
	}

	void SetupPostprocessBlurGaussian(RenderGraph& graph, const String& input, String& out, const AttachmentInfo& attchment)
	{
		// Replace 2D Gaussian blur with 1D Gaussian blur twice
		auto& blurPass = graph.AddRenderPass("BlurPass", RenderGraphQueueFlag::Compute);
		auto& readRes = blurPass.ReadTexture(input.c_str());
		auto& tempRes = blurPass.WriteStorageTexture("rtBlurTemp", attchment, "rtBlurTemp");
		tempRes.SetImageUsage(VK_IMAGE_USAGE_SAMPLED_BIT);

		auto& output = blurPass.WriteStorageTexture("rtBlur", attchment);
		blurPass.SetBuildCallback([&](GPU::CommandList& cmd) {
			auto& read = graph.GetPhysicalTexture(readRes);
			auto& temp = graph.GetPhysicalTexture(tempRes);
			auto& out = graph.GetPhysicalTexture(output);

			cmd.SetProgram(Renderer::GetShader(SHADERTYPE_CS_POSTPROCESS_BLUR_GAUSSIAN));

			PostprocessPushConstants push;
			push.resolution = {
				out.GetImage()->GetCreateInfo().width,
				out.GetImage()->GetCreateInfo().height
			};
			push.resolution_rcp = {
				1.0f / push.resolution.x,
				1.0f / push.resolution.y,
			};

			// Horizontal:
			push.params0.x = 1.0f;
			push.params0.y = 0.0f;

			cmd.PushConstants(&push, 0, sizeof(push));
			cmd.SetTexture(0, 0, read);
			cmd.SetStorageTexture(0, 0, temp);

			cmd.Dispatch(
				(push.resolution.x + POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT - 1) / POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT,
				push.resolution.y,
				1
			);

			cmd.ImageBarrier(*temp.GetImage(),
				VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT);
			temp.GetImage()->SetLayoutType(GPU::ImageLayoutType::Optimal);

			// Vertical:
			push.params0.x = 0.0f;
			push.params0.y = 1.0f;

			cmd.PushConstants(&push, 0, sizeof(push));
			cmd.SetTexture(0, 0, temp);
			cmd.SetStorageTexture(0, 0, out);
			cmd.Dispatch(
				push.resolution.x,
				(push.resolution.y + POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT - 1) / POSTPROCESS_BLUR_GAUSSIAN_THREADCOUNT,
				1
			);

			cmd.ImageBarrier(*temp.GetImage(),
				VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_READ_BIT,
				VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT);
			temp.GetImage()->SetLayoutType(GPU::ImageLayoutType::General);
		});
		out = "rtBlur";
	}

	void PostprocessOutline(GPU::CommandList& cmd, const GPU::ImageView& texture, F32 threshold, F32 thickness, const Color4& color)
	{
		cmd.SetRasterizerState(GetRasterizerState(RSTYPE_DOUBLE_SIDED));
		cmd.SetBlendState(GetBlendState(BSTYPE_TRANSPARENT));
		cmd.SetDepthStencilState(GetDepthStencilState(DSTYPE_DISABLED));
		cmd.SetTexture(0, 0, texture);
		cmd.SetProgram(
			GetShader(SHADERTYPE_VS_POSTPROCESS),
			GetShader(SHADERTYPE_PS_POSTPROCESS_OUTLINE)
		);

		PostprocessPushConstants push;
		push.resolution = {
			texture.GetImage()->GetCreateInfo().width,
			texture.GetImage()->GetCreateInfo().height
		};
		push.resolution_rcp = {
			1.0f / push.resolution.x,
			1.0f / push.resolution.y,
		};
		push.params0.x = threshold;
		push.params0.y = thickness;
		push.params1 = color.ToFloat4();
		cmd.PushConstants(&push, 0, sizeof(push));

		cmd.Draw(3);
	}

	RendererPlugin* CreatePlugin(Engine& engine)
	{
		rendererPlugin = CJING_NEW(RendererPluginImpl)(engine);
		return rendererPlugin;
	}
}
}
