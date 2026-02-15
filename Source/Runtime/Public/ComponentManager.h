#pragma once
#include "Types.h"
#include "Entity.h"

namespace won::ecs
{
    class IComponentArray
    {
    public:
        virtual ~IComponentArray() = default;
        virtual void EntityDestroyed(Entity entity) = 0;
    };

    template <typename T>
    class ComponentArray : public IComponentArray
    {
    public:
        void Insert(Entity entity, T component)
        {
            // map entity to array index
            entity_to_index[entity] = data.size();
            index_to_entity[data.size()] = entity;
            data.push_back(component);
        }

        void Remove(Entity entity)
        {
            if (!HasData(entity))
            {
                return;
            }

            // swap with last element for fast removal (O(1))
            Size index_to_remove = entity_to_index[entity];
            Size last_index = data.size() - 1;
            data[index_to_remove] = data[last_index];

            // Update mappings
            Entity last_entity = index_to_entity[last_index];
            entity_to_index[last_entity] = index_to_remove;
            index_to_entity[index_to_remove] = last_entity;

            entity_to_index.erase(entity);
            index_to_entity.erase(last_index);
            data.pop_back();
        }

        T& GetData(Entity entity)
        {
            return data[entity_to_index[entity]];
        }

        bool HasData(Entity entity) const
        {
            return entity_to_index.find(entity) != entity_to_index.end();
        }

        void EntityDestroyed(Entity entity) override
        {
            if (entity_to_index.find(entity) != entity_to_index.end())
            {
                Remove(entity);
            }
        }

    private:
        Vector<T> data;
        UnorderedMap<Entity, Size> entity_to_index;
        UnorderedMap<Size, Entity> index_to_entity;
    };

    class ComponentManager {
    public:
        template <typename T>
        void RegisterComponent()
        {
            const String type_name = typeid(T).name();
            auto it = component_arrays.find(type_name);
            if (it != component_arrays.end() && it->second)
            {
                return;
            }
            component_arrays[type_name] = std::make_shared<ComponentArray<T>>();
        }

        template <typename T>
        T* AddComponent(Entity entity, T component)
        {
            auto component_array = GetComponentArray<T>();
            if (!component_array)
            {
                RegisterComponent<T>();
                component_array = GetComponentArray<T>();
                if (!component_array)
                {
                    return nullptr;
                }
            }

            if (component_array->HasData(entity))
            {
                component_array->GetData(entity) = component;
                return &component_array->GetData(entity);
            }

            component_array->Insert(entity, component);
            return &component_array->GetData(entity);
        }

        template <typename T>
        void RemoveComponent(Entity entity)
        {
            auto component_array = GetComponentArray<T>();
            if (!component_array)
            {
                return;
            }
            component_array->Remove(entity);
        }

        template <typename T>
        T* GetComponent(Entity entity)
        {
            auto component_array = GetComponentArray<T>();
            if (!component_array || !component_array->HasData(entity))
            {
                return nullptr;
            }
            return &component_array->GetData(entity);
        }

        template <typename T>
        bool HasComponent(Entity entity) const
        {
            auto component_array = GetComponentArray<T>();
            if (!component_array)
            {
                return false;
            }
            return component_array->HasData(entity);
        }

        void EntityDestroyed(Entity entity)
        {
            for (auto const& pair : component_arrays)
            {
                pair.second->EntityDestroyed(entity);
            }
        }

        template <typename T>
        std::shared_ptr<ComponentArray<T>> GetComponentArray()
        {
            const String type_name = typeid(T).name();
            auto it = component_arrays.find(type_name);
            if (it == component_arrays.end() || !it->second)
            {
                return nullptr;
            }
            return std::static_pointer_cast<ComponentArray<T>>(it->second);
        }

        template <typename T>
        std::shared_ptr<const ComponentArray<T>> GetComponentArray() const
        {
            const String type_name = typeid(T).name();
            auto it = component_arrays.find(type_name);
            if (it == component_arrays.end() || !it->second)
            {
                return nullptr;
            }
            return std::static_pointer_cast<const ComponentArray<T>>(it->second);
        }

    private:
        UnorderedMap<String, std::shared_ptr<IComponentArray>> component_arrays;
    };
}
