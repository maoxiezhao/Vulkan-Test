#pragma once

namespace VulkanTest
{
	enum RasterizerStateTypes
	{
		RSTYPE_BACK,
		RSTYPE_FRONT,
		RSTYPE_DOUBLE_SIDED,
		RSTYPE_COUNT
	};

	enum BlendStateTypes
	{
		BSTYPE_OPAQUE,
		BSTYPE_TRANSPARENT,
		BSTYPE_PREMULTIPLIED,
		BSTYPE_COUNT
	};

	enum DepthStencilStateType
	{
		DSTYPE_DISABLED,
		DSTYPE_DEFAULT,
		DSTYPE_READEQUAL,
		DSTYPE_COUNT
	};

	enum ShaderType
	{
		SHADERTYPE_VS_OBJECT,
		SHADERTYPE_VS_PREPASS,
		SHADERTYPE_VS_VERTEXCOLOR,
		SHADERTYPE_VS_POSTPROCESS,

		SHADERTYPE_CS_POSTPROCESS_BLUR_GAUSSIAN,

		SHADERTYPE_PS_OBJECT,
		SHADERTYPE_PS_PREPASS,
		SHADERTYPE_PS_VERTEXCOLOR,
		SHADERTYPE_PS_POSTPROCESS_OUTLINE,

		SHADERTYPE_COUNT
	};

	enum RENDERPASS
	{
		RENDERPASS_MAIN,
		RENDERPASS_PREPASS,
		RENDERPASS_SHADOE,
		RENDERPASS_COUNT
	};

	enum BlendMode
	{
		BLENDMODE_OPAQUE,
		BLENDMODE_ALPHA,
		BLENDMODE_PREMULTIPLIED,
		BLENDMODE_COUNT
	};

	enum ObjectDoubleSided
	{
		OBJECT_DOUBLESIDED_FRONTSIDE,
		OBJECT_DOUBLESIDED_ENABLED,
		OBJECT_DOUBLESIDED_BACKSIDE,
		OBJECT_DOUBLESIDED_COUNT
	};
}