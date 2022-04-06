#include "world.h"

namespace VulkanTest
{
	World::World(Engine* engine_) :
		engine(engine_)
	{
		world = ECS::World::Create();
	}

	World::~World()
	{
	}

	const ECS::EntityBuilder& World::CreateEntity(const char* name)
	{
		return world->CreateEntity(name);
	}

	const ECS::EntityBuilder& World::CreatePrefab(const char* name)
	{
		return world->CreatePrefab(name);
	}

	ECS::EntityID World::CreateEntityID(const char* name)
	{
		return world->CreateEntityID(name);
	}

	ECS::EntityID World::FindEntity(const char* name)
	{
		return world->FindEntityIDByName(name);
	}

	ECS::EntityID World::IsEntityAlive(ECS::EntityID entity) const
	{
		return world->IsEntityAlive(entity);
	}

	void World::DeleteEntity(ECS::EntityID entity)
	{
		return world->DeleteEntity(entity);
	}

	void World::SetEntityName(ECS::EntityID entity, const char* name)
	{
		world->SetEntityName(entity, name);
	}

	bool World::HasComponent(ECS::EntityID entity, ECS::EntityID compID)
	{
		return world->HasComponent(entity, compID);
	}

	IScene* World::GetScene(const char* name) const
	{
		return nullptr;
	}

	void World::AddScene(UniquePtr<IScene>&& scene)
	{
	}

	std::vector<UniquePtr<IScene>>& World::GetScenes()
	{
		return scenes;
	}
}