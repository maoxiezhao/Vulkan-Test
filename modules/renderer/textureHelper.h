#pragma once

#include "texture.h"

namespace VulkanTest
{
	namespace TextureHelper
	{
		void Initialize(ResourceManager* resourceManager_);
		void Uninitialize();

		Texture* GetColor(const Color4& color);
	}
}