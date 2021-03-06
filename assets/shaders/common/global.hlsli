#ifndef SHADER_GLOBAL_HF
#define SHADER_GLOBAL_HF

#include "shaderInterop.h"
#include "shaderInterop_renderer.h"

ByteAddressBuffer bindless_buffers[] : register(space1);

SamplerState samLinearClamp : register(s100);
SamplerState samLinearWrap  : register(s101);
SamplerState samPointClamp  : register(s102);
SamplerState samPointWrap   : register(s103);

inline ShaderSceneCB GetScene()
{
	return g_xFrame.scene;
}

inline CameraCB GetCamera()
{
	return g_xCamera;
}

inline ShaderGeometry LoadGeometry(uint geometryIndex)
{
	return bindless_buffers[GetScene().geometrybuffer].Load<ShaderGeometry>(geometryIndex * sizeof(ShaderGeometry));
}

inline ShaderMaterial LoadMaterial(uint materialIndex)
{
	return bindless_buffers[GetScene().materialbuffer].Load<ShaderMaterial>(materialIndex * sizeof(ShaderMaterial));
}

inline ShaderMeshInstance LoadInstance(uint instanceIndex)
{
	return bindless_buffers[GetScene().instancebuffer].Load<ShaderMeshInstance>(instanceIndex * sizeof(ShaderMeshInstance));

}

#endif