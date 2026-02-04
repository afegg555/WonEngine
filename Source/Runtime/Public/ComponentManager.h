#pragma once
#include "Types.h"
#include "Entity.h"

namespace won::ecs
{
    class IComponentArray {
    public:
        virtual ~IComponentArray() = default;
        virtual void EntityDestroyed(Entity entity) = 0;
    };

    template <typename T>
    class ComponentArray : public IComponentArray {
    public:
        void Insert(Entity entity, T component) {
            // map entity to array index
            entity_to_index[entity] = data.size();
            index_to_entity[data.size()] = entity;
            data.push_back(component);
        }

        void Remove(Entity entity) {

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

        T& GetData(Entity entity) { return data[entity_to_index[entity]]; }

        void EntityDestroyed(Entity entity) override {
            if (entity_to_index.find(entity) != entity_to_index.end()) {
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
        void RegisterComponent() {
            const char* type_name = typeid(T).name();
            component_arrays[type_name] = std::make_shared<ComponentArray<T>>();
        }

        template <typename T>
        void AddComponent(Entity entity, T component) {
            GetComponentArray<T>()->Insert(entity, component);
        }

        template <typename T>
        void RemoveComponent(Entity entity) {
            GetComponentArray<T>()->Remove(entity);
        }

        template <typename T>
        T& GetComponent(Entity entity) {
            return GetComponentArray<T>()->GetData(entity);
        }

        void EntityDestroyed(Entity entity) {
            for (auto const& pair : component_arrays) {
                pair.second->EntityDestroyed(entity);
            }
        }

        template <typename T>
        std::shared_ptr<ComponentArray<T>> GetComponentArray() {
            return std::static_pointer_cast<ComponentArray<T>>(component_arrays[typeid(T).name()]);
        }
    private:
        std::unordered_map<const char*, std::shared_ptr<IComponentArray>> component_arrays;
    };
}