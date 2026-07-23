#pragma once
#include "Types.h"
#include "Entity.h"
#include "TypeMeta.h"
#include "PoolAllocator.h"

#include <cstring>

namespace won::ecs
{
    class IComponentArray
    {
    public:
        virtual ~IComponentArray() = default;
        virtual void Insert(Entity entity, const void* component) = 0;
        virtual void Remove(Entity entity) = 0;
        virtual bool HasData(Entity entity) const = 0;
        virtual void* GetRawData(Entity entity) = 0;
        virtual const void* GetRawData(Entity entity) const = 0;
        virtual const won::TypeDesc* GetTypeDesc() const = 0;
        virtual Entity GetEntity(Size index) const = 0;
        virtual Size GetSize() const = 0;
        virtual void Clear() = 0;
    };

    template <typename T>
    class ComponentArray : public IComponentArray
    {
    public:
        void Insert(Entity entity, T component)
        {
            // map entity to array index
            entity_to_index[entity] = data.size();
            index_to_entity.push_back(entity);
            data.push_back(component);
        }

        void Insert(Entity entity, const void* component) override
        {
            if (!component)
            {
                Insert(entity, T{});
                return;
            }
            Insert(entity, *static_cast<const T*>(component));
        }

        void Remove(Entity entity) override
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
            index_to_entity.pop_back();
            data.pop_back();
        }

        T& GetData(Entity entity)
        {
            return data[entity_to_index[entity]];
        }

        const T& GetData(Entity entity) const
        {
            return data[entity_to_index.at(entity)];
        }

        void* GetRawData(Entity entity) override
        {
            return HasData(entity) ? &GetData(entity) : nullptr;
        }

        const void* GetRawData(Entity entity) const override
        {
            auto it = entity_to_index.find(entity);
            if (it == entity_to_index.end())
            {
                return nullptr;
            }
            return &data[it->second];
        }

        bool HasData(Entity entity) const override
        {
            return entity_to_index.find(entity) != entity_to_index.end();
        }

        const won::TypeDesc* GetTypeDesc() const override
        {
            return reflection::TypeMeta<T>::Get();
        }

        Size GetSize() const override
        {
            return data.size();
        }

        Entity GetEntity(Size index) const override
        {
            return index < index_to_entity.size() ? index_to_entity[index] : INVALID_ENTITY;
        }

        void Clear() override
        {
            data.clear();
            entity_to_index.clear();
            index_to_entity.clear();
        }

        Vector<T> data;
        UnorderedMap<Entity, Size> entity_to_index;
        Vector<Entity> index_to_entity;
    };

    class DynamicComponentArray : public IComponentArray
    {
    public:
        explicit DynamicComponentArray(const won::TypeDesc* type_desc_in)
            : type_desc(type_desc_in)
            , allocator(type_desc_in ? type_desc_in->size : 1, type_desc_in ? type_desc_in->alignment : 1)
        {
        }

        ~DynamicComponentArray() override
        {
            Clear();
        }

        DynamicComponentArray(const DynamicComponentArray&) = delete;
        DynamicComponentArray& operator=(const DynamicComponentArray&) = delete;

        void Insert(Entity entity, const void* component) override
        {
            if (!type_desc)
            {
                return;
            }

            void* memory = allocator.Allocate();
            if (!memory)
            {
                return;
            }

            if (component && type_desc->Copy)
            {
                type_desc->Copy(memory, component);
            }
            else if (component)
            {
                std::memcpy(memory, component, type_desc->size);
            }
            else if (type_desc->Construct)
            {
                type_desc->Construct(memory);
            }
            else
            {
                std::memset(memory, 0, type_desc->size);
            }
            entity_to_index[entity] = data.size();
            index_to_entity.push_back(entity);
            data.push_back(memory);
        }

        void Remove(Entity entity) override
        {
            if (!HasData(entity))
            {
                return;
            }

            const Size index_to_remove = entity_to_index[entity];
            const Size last_index = data.size() - 1;
            void* removed_component = data[index_to_remove];

            if (removed_component && type_desc->Destruct)
            {
                type_desc->Destruct(removed_component);
            }
            allocator.Deallocate(removed_component);

            if (index_to_remove != last_index)
            {
                data[index_to_remove] = data[last_index];
                Entity last_entity = index_to_entity[last_index];
                entity_to_index[last_entity] = index_to_remove;
                index_to_entity[index_to_remove] = last_entity;
            }

            entity_to_index.erase(entity);
            index_to_entity.pop_back();
            data.pop_back();
        }

        bool HasData(Entity entity) const override
        {
            return entity_to_index.find(entity) != entity_to_index.end();
        }

        void* GetRawData(Entity entity) override
        {
            auto it = entity_to_index.find(entity);
            if (it == entity_to_index.end())
            {
                return nullptr;
            }
            return data[it->second];
        }

        const void* GetRawData(Entity entity) const override
        {
            auto it = entity_to_index.find(entity);
            if (it == entity_to_index.end())
            {
                return nullptr;
            }
            return data[it->second];
        }

        const won::TypeDesc* GetTypeDesc() const override
        {
            return type_desc;
        }

        Size GetSize() const override
        {
            return data.size();
        }

        Entity GetEntity(Size index) const override
        {
            return index < index_to_entity.size() ? index_to_entity[index] : INVALID_ENTITY;
        }

        void Clear() override
        {
            if (type_desc)
            {
                for (void* component : data)
                {
                    if (component && type_desc->Destruct)
                    {
                        type_desc->Destruct(component);
                    }
                    allocator.Deallocate(component);
                }
            }
            data.clear();
            entity_to_index.clear();
            index_to_entity.clear();
        }

    private:
        const won::TypeDesc* type_desc = nullptr;
        memory::PoolAllocator allocator;
        Vector<void*> data;
        UnorderedMap<Entity, Size> entity_to_index;
        Vector<Entity> index_to_entity;
    };

    class ComponentManager {
    public:
        template <typename T>
        void RegisterComponent()
        {
            const won::TypeDesc* type_desc = reflection::TypeMeta<T>::Get();
            if (!type_desc || type_desc->type_id == 0)
            {
                return;
            }

            auto it = component_arrays.find(type_desc->type_id);
            if (it != component_arrays.end() && it->second)
            {
                return;
            }
            component_arrays[type_desc->type_id] = std::make_shared<ComponentArray<T>>();
        }

        void RegisterComponent(const won::TypeDesc* type_desc)
        {
            if (!type_desc || type_desc->struct_size < sizeof(won::TypeDesc) || type_desc->type_id == 0 || !type_desc->name || type_desc->name[0] == '\0' || type_desc->size == 0 || type_desc->alignment == 0 || (type_desc->alignment & (type_desc->alignment - 1)) != 0)
            {
                return;
            }

            auto it = component_arrays.find(type_desc->type_id);
            if (it != component_arrays.end() && it->second)
            {
                return;
            }
            component_arrays[type_desc->type_id] = std::make_shared<DynamicComponentArray>(type_desc);
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

        void* AddComponent(Entity entity, won::TypeId type_id, const void* component)
        {
            if (type_id == 0)
            {
                return nullptr;
            }

            auto component_array = GetComponentArray(type_id);
            if (!component_array)
            {
                return nullptr;
            }

            if (component_array->HasData(entity))
            {
                component_array->Remove(entity);
            }

            component_array->Insert(entity, component);
            return component_array->GetRawData(entity);
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

        void RemoveComponent(Entity entity, won::TypeId type_id)
        {
            auto component_array = GetComponentArray(type_id);
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

        void* GetComponent(Entity entity, won::TypeId type_id)
        {
            auto component_array = GetComponentArray(type_id);
            if (!component_array)
            {
                return nullptr;
            }
            return component_array->GetRawData(entity);
        }

        const void* GetComponent(Entity entity, won::TypeId type_id) const
        {
            auto component_array = GetComponentArray(type_id);
            if (!component_array)
            {
                return nullptr;
            }
            return component_array->GetRawData(entity);
        }

        template <typename T>
        bool HasComponent(Entity entity) const
        {
            const won::TypeDesc* type_desc = reflection::TypeMeta<T>::Get();
            if (!type_desc || type_desc->type_id == 0)
            {
                return false;
            }
            return HasComponent(entity, type_desc->type_id);
        }

        bool HasComponent(Entity entity, won::TypeId type_id) const
        {
            auto component_array = GetComponentArray(type_id);
            if (!component_array)
            {
                return false;
            }
            return component_array->HasData(entity);
        }

        Vector<const won::TypeDesc*> GetComponentTypes() const
        {
            Vector<const won::TypeDesc*> component_types;
            component_types.reserve(component_arrays.size());
            for (const auto& pair : component_arrays)
            {
                if (pair.second)
                {
                    component_types.push_back(pair.second->GetTypeDesc());
                }
            }
            return component_types;
        }

        void RemoveComponents(Entity entity)
        {
            for (auto const& pair : component_arrays)
            {
                pair.second->Remove(entity);
            }
        }

        void Clear()
        {
            for (auto const& pair : component_arrays)
            {
                if (pair.second)
                {
                    pair.second->Clear();
                }
            }
        }

        template <typename T>
        std::shared_ptr<ComponentArray<T>> GetComponentArray()
        {
            const won::TypeDesc* type_desc = reflection::TypeMeta<T>::Get();
            if (!type_desc || type_desc->type_id == 0)
            {
                return nullptr;
            }

            auto it = component_arrays.find(type_desc->type_id);
            if (it == component_arrays.end() || !it->second)
            {
                return nullptr;
            }
            return std::static_pointer_cast<ComponentArray<T>>(it->second);
        }

        template <typename T>
        std::shared_ptr<const ComponentArray<T>> GetComponentArray() const
        {
            const won::TypeDesc* type_desc = reflection::TypeMeta<T>::Get();
            if (!type_desc || type_desc->type_id == 0)
            {
                return nullptr;
            }

            auto it = component_arrays.find(type_desc->type_id);
            if (it == component_arrays.end() || !it->second)
            {
                return nullptr;
            }
            return std::static_pointer_cast<const ComponentArray<T>>(it->second);
        }

        std::shared_ptr<IComponentArray> GetComponentArray(won::TypeId type_id)
        {
            if (type_id == 0)
            {
                return nullptr;
            }

            auto it = component_arrays.find(type_id);
            if (it == component_arrays.end() || !it->second)
            {
                return nullptr;
            }
            return it->second;
        }

        std::shared_ptr<const IComponentArray> GetComponentArray(won::TypeId type_id) const
        {
            if (type_id == 0)
            {
                return nullptr;
            }

            auto it = component_arrays.find(type_id);
            if (it == component_arrays.end() || !it->second)
            {
                return nullptr;
            }
            return it->second;
        }

    private:
        UnorderedMap<won::TypeId, std::shared_ptr<IComponentArray>> component_arrays;
    };
}
