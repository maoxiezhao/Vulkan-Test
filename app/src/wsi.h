#pragma once

#include "common.h"
#include "vulkan\definition.h"

class Platform;

class WSI
{
public:
	bool Initialize();
	void Uninitialize();
	void BeginFrame();
	void EndFrame();
	void SetPlatform(Platform* platform);
	
	DeviceVulkan* GetDevice();

private:
	Platform* mPlatform = nullptr;
};