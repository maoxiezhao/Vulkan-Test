#pragma once

#include "editor\common.h"
#include "editor\editorPlugin.h"
#include "client\app\app.h"

namespace VulkanTest
{
namespace Editor
{
    class AssetCompiler;

    class VULKAN_EDITOR_API EditorApp : public App
    {
    public:
        static EditorApp* Create();
        static void Destroy(EditorApp* app);

        virtual void AddPlugin(EditorPlugin& plugin) = 0;
        virtual void AddWidget(EditorWidget& widget) = 0;
        virtual void RemoveWidget(EditorWidget& widget) = 0;

        virtual void AddWindow(Platform::WindowType window) = 0;
        virtual void RemoveWindow(Platform::WindowType window) = 0;
        virtual void DeferredDestroyWindow(Platform::WindowType window) = 0;

        virtual void SaveSettings() = 0;

        virtual AssetCompiler& GetAssetCompiler() = 0;
    };
}   
} 
