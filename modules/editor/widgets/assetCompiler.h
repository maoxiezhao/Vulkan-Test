#pragma once

#include "editor\common.h"
#include "editor\editorPlugin.h"
#include "core\filesystem\filesystem.h"
#include "core\resource\resource.h"
#include "core\resource\resourceManager.h"

namespace VulkanTest
{
namespace Editor
{
    class EditorApp;

    class VULKAN_EDITOR_API AssetCompiler : public EditorWidget
    {
    public:
        struct VULKAN_EDITOR_API IPlugin
        {
            virtual ~IPlugin() {}
            virtual bool Compile(const Path& path) = 0;
            virtual void RegisterResource(AssetCompiler& compiler, const char* path);
            virtual std::vector<const char*> GetSupportExtensions() = 0;
        };

        struct ResourceItem 
        {
            Path path;
            ResourceType type;
            RuntimeHash dirHash;
        };

        static UniquePtr<AssetCompiler> Create(EditorApp& app);

        virtual ~AssetCompiler() {};

        virtual void AddPlugin(IPlugin& plugin, const std::vector<const char*>& exts) = 0;
        virtual void AddPlugin(IPlugin& plugin) = 0;
        virtual void RemovePlugin(IPlugin& plugin) = 0;
        virtual void AddDependency(const Path& parent, const Path& dep) = 0;
        virtual bool Compile(const Path& path) = 0;
        virtual bool CopyCompile(const Path& path) = 0;
        virtual bool WriteCompiled(const char* path, Span<const U8>data) = 0;
        virtual ResourceType GetResourceType(const char* path) const = 0;
        virtual void RegisterExtension(const char* extension, ResourceType type) = 0;
        virtual void AddResource(ResourceType type, const char* path) = 0;
        virtual const HashMap<U64, ResourceItem>& LockResources() = 0;
        virtual void UnlockResources() = 0;
        virtual DelegateList<void(const Path& path)>& GetListChangedCallback() = 0;
    };
}
}