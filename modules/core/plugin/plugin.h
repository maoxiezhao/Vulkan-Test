#pragma once

#include "core\common.h"
#include "core\memory\memory.h"

namespace VulkanTest
{
	class Engine;

	class VULKAN_TEST_API IPlugin
	{
	public:
		virtual ~IPlugin() = default;

		virtual void Initialize() {}
		virtual void Update(F32 delta) {}
		virtual void OnAdded(IPlugin& plugin) {}
		virtual void OnGameStart() {}
		virtual void OnGameStop() {}
	};

	class VULKAN_TEST_API PluginManager
	{
	public:
		static UniquePtr<PluginManager> Create(Engine& engine);
	};
}