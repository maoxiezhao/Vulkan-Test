#pragma once

#include "resource.h"

namespace VulkanTest
{
	class VULKAN_TEST_API ResourceFactory
	{
	public:
		friend class Resource;
		friend class ResourceManager;
		using ResourceTable = std::unordered_map<U64, Resource*>;
		
		ResourceFactory();
		virtual ~ResourceFactory();

		void Initialize(ResourceType type, ResourceManager& resManager_);
		void Uninitialize();

		bool IsUnloadEnable()const {
			return isUnloadEnable;
		}

	protected:
		Resource* LoadResource(const Path& path);
		Resource* GetResource(const Path& path);

		virtual Resource* CreateResource(const Path& path) = 0;
		virtual void DestroyResource(Resource* res) = 0;

	private:
		ResourceTable resources;
		bool isUnloadEnable;
		ResourceType resType;
		ResourceManager* resManager;
	};

	class VULKAN_TEST_API ResourceManager
	{
	public:
		using FactoryTable = std::unordered_map<U64, ResourceFactory*>;

		ResourceManager();
		virtual ~ResourceManager();

		void Initialize(class FileSystem& fileSystem_);
		void Uninitialzie();

		template<typename T>
		T* LoadResource(const Path& path)
		{
			return static_cast<T*>(LoadResource(T::ResType, path));
		}

		Resource* LoadResource(ResourceType type, const Path& path);

		ResourceFactory* GetFactory(ResourceType type);
		void RegisterFactory(ResourceType type, ResourceFactory* factory);
		void UnregisterFactory(ResourceType type);

	private:
		friend class Resource;

		class FileSystem* fileSystem = nullptr;
		FactoryTable factoryTable;
		bool isInitialized = false;
	};
}