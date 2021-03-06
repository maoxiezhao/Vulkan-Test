#ifndef SHADER_OBJECT_HF
#define SHADER_OBJECT_HF

#include "global.hlsli"
#include "surface.hlsli"

// Use these to define the expected layout for the shader:
//#define OBJECTSHADER_LAYOUT_PREPASS			- layout for prepass
//#define OBJECTSHADER_LAYOUT_COMMON			- layout for common passes

#ifdef OBJECTSHADER_LAYOUT_PREPASS
#define OBJECTSHADER_USE_INSTANCEINDEX
#endif

#ifdef OBJECTSHADER_LAYOUT_COMMON
#define OBJECTSHADER_USE_COLOR
#define OBJECTSHADER_USE_NORMAL
#define OBJECTSHADER_USE_POSITION3D
#define OBJECTSHADER_USE_INSTANCEINDEX
#endif

PUSHCONSTANT(push, ObjectPushConstants)

inline ShaderGeometry GetMesh()
{
	return LoadGeometry(push.geometryIndex);
}

inline ShaderMaterial GetMaterial()
{
    return LoadMaterial(push.materialIndex);
}

struct VertexInput
{
	uint vertexID : SV_VertexID;
    uint instanceID : SV_InstanceID;

	float4 GetPosition()
	{
		return float4(bindless_buffers[GetMesh().vbPos].Load<float3>(vertexID * sizeof(float3)), 1);
	}

    float3 GetNormal()
    {
        return bindless_buffers[GetMesh().vbNor].Load<float3>(vertexID * sizeof(float3));
    }

    float2 GetUVSets()
    {   
        [branch]
		if (GetMesh().vbUVs < 0)
			return 0;
        
        return bindless_buffers[GetMesh().vbUVs].Load<float2>(vertexID * sizeof(float2));
    }

    ShaderMeshInstancePointer GetInstancePointer()
	{
		if (push.instance >= 0)
			return bindless_buffers[push.instance].Load<ShaderMeshInstancePointer>(push.instanceOffset + instanceID * sizeof(ShaderMeshInstancePointer));

		ShaderMeshInstancePointer pointer;
		pointer.init();
		return pointer;
	}

    ShaderMeshInstance GetInstance()
    {
        if (push.instance >= 0)
            return LoadInstance(GetInstancePointer().instanceIndex);

        ShaderMeshInstance inst;
        inst.init();
        return inst;
    }
};

struct VertexSurface
{
    float4 position;
    float2 uv;
    float3 normal;
	float4 color;

    inline void Create(in VertexInput input)
    {
        position = input.GetPosition();
        uv = input.GetUVSets();
        normal = input.GetNormal();
		color = float4(1.0f, 1.0f, 1.0f, 1.0f);
        position = mul(input.GetInstance().transform.GetMatrix(), position);
    }
};

struct PixelInput
{
    float4 pos : SV_POSITION;

#ifdef OBJECTSHADER_USE_COLOR
	float4 color : COLOR;
#endif

#ifdef OBJECTSHADER_USE_POSITION3D
	float3 pos3D : WORLDPOSITION;
#endif

#ifdef OBJECTSHADER_USE_NORMAL
	float3 nor : NORMAL;
#endif

#ifdef OBJECTSHADER_USE_INSTANCEINDEX
	uint instanceIndex : INSTANCEINDEX;
#endif
};

#endif