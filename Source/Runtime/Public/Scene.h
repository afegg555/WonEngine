#pragma once
#include "ComponentManager.h"
#include "Entity.h"
#include "System.h"
#include "Types.h"

#include <algorithm>
#include <memory>
#include <utility>

namespace won::ecs
{
    class Scene
    {
    public:
        Entity CreateEntity()
        {
            Entity entity = ecs::CreateEntity();
            entities.push_back(entity);
            return entity;
        }

        void DestroyEntity(Entity entity)
        {
            component_manager.EntityDestroyed(entity);
            entities.erase(
                std::remove_if(
                    entities.begin(),
                    entities.end(),
                    [entity](const Entity& current)
                    {
                        return current == entity;
                    }),
                entities.end());
        }

        template <typename Component, typename... Args>
        Component* AddComponent(Entity entity, Args&&... args)
        {
            Component component { std::forward<Args>(args)... };
            return component_manager.AddComponent<Component>(entity, component);
        }

        template <typename Component>
        Component* GetComponent(Entity entity)
        {
            return component_manager.GetComponent<Component>(entity);
        }

        template <typename Component>
        bool HasComponent(Entity entity) const
        {
            return component_manager.HasComponent<Component>(entity);
        }

        void AddSystem(const std::shared_ptr<System>& system)
        {
            if (system)
            {
                systems.push_back(system);
            }
        }

        void Update(float delta_time)
        {
            for (const auto& system : systems)
            {
                if (system)
                {
                    system->Update(*this, delta_time);
                }
            }
        }

        const Vector<Entity>& GetEntities() const
        {
            return entities;
        }

    private:
        ComponentManager component_manager;
        Vector<Entity> entities;
        Vector<std::shared_ptr<System>> systems;
    };
}
