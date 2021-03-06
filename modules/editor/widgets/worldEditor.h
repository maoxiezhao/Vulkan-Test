#pragma once

#include "editor\common.h"
#include "editor\editorPlugin.h"
#include "core\scene\world.h"
#include "renderer\renderScene.h"

namespace VulkanTest
{
namespace Editor
{
    class EditorApp;

    struct VULKAN_EDITOR_API EntityFolder
    {
        struct Folder 
        {
            ECS::EntityID firstEntity = ECS::INVALID_ENTITY;
            char name[116];
        };

        struct FolderEntity 
        {
            ECS::EntityID next = ECS::INVALID_ENTITY;
            ECS::EntityID prev = ECS::INVALID_ENTITY;
        };

        EntityFolder(World& world_);
        ~EntityFolder();

        void MoveToRootFolder(ECS::EntityID entity);
        ECS::EntityID GetNextEntity(ECS::EntityID e) const;
            
        Folder& GetRootFolder() {
            return rootFolder;
        }

    private:
        void OnEntityCreated(ECS::EntityID e);
        void OnEntityDestroyed(ECS::EntityID e);

        World& world;
        Folder rootFolder;
        Array<FolderEntity> entities;
    };

    struct VULKAN_EDITOR_API WorldView
    {
        virtual ~WorldView() = default;

        virtual bool IsMouseDown(Platform::MouseButton button) const = 0;
        virtual bool IsMouseClick(Platform::MouseButton button) const = 0;
        virtual F32x2 GetMousePos() const = 0;
        virtual const CameraComponent& GetCamera()const = 0;
    };

    class VULKAN_EDITOR_API WorldEditor : public EditorWidget
    {
    public:
        static UniquePtr<WorldEditor> Create(EditorApp& app);

        virtual ~WorldEditor() {};

        virtual World* GetWorld() = 0;
        virtual void NewWorld() = 0;
        virtual void DestroyWorld() = 0;

        virtual void SelectEntities(Span<const ECS::EntityID> entities, bool toggle) = 0;

        virtual EntityFolder& GetEntityFolder() = 0;
        virtual const Array<ECS::EntityID>& GetSelectedEntities() const = 0;
    };
}
}