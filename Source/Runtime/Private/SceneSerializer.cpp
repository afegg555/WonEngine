#include "SceneSerializer.h"
#include "Backlog.h"
#include "Reflection.h"
#include "Scene.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace won::serialize
{
    namespace
    {
        constexpr uint32 scene_format_version = 1;
        constexpr uint64 invalid_entity_index = static_cast<uint64>(-1);

        void WriteScene(JsonArchive& archive, const ecs::Scene& scene, const SceneSerializeDesc& desc)
        {
            archive.BeginObject(); 
            uint32 version = scene_format_version;
            archive.Field("version", version); 

            UnorderedMap<ecs::Entity, bool> excluded_entity_map;
            if (desc.excluded_entities)
            {
                excluded_entity_map.reserve(desc.excluded_entities->size());
                for (ecs::Entity entity : *desc.excluded_entities)
                {
                    excluded_entity_map[entity] = true;
                }
            }

            UnorderedMap<ecs::Entity, uint64> entity_to_index;
            const Vector<ecs::Entity>& entities = scene.GetEntities();
            uint64 entity_count = 0;
            for (ecs::Entity entity : entities)
            {
                if (excluded_entity_map.find(entity) != excluded_entity_map.end())
                {
                    continue;
                }
                entity_to_index[entity] = entity_count;
                ++entity_count;
            }
            archive.Field("entity_count", entity_count);

            Vector<const won::TypeDesc*> component_types = scene.GetComponentTypes();
            std::sort(component_types.begin(), component_types.end(), [](const won::TypeDesc* lhs, const won::TypeDesc* rhs) {
                const won::TypeId lhs_id = lhs ? lhs->type_id : 0;
                const won::TypeId rhs_id = rhs ? rhs->type_id : 0;
                return lhs_id < rhs_id;
            });

            archive.BeginObject("components");

            for (const won::TypeDesc* type_desc : component_types)
            {
                if (!type_desc)
                {
                    continue;
                }

                std::shared_ptr<const ecs::IComponentArray> component_array = scene.GetComponentArray(type_desc->type_id);
                if (!component_array || component_array->GetSize() == 0)
                {
                    continue;
                }

                Vector<ecs::Entity> component_entities;
                Vector<uint64> item_entity_indices;
                component_entities.reserve(component_array->GetSize());
                item_entity_indices.reserve(component_array->GetSize());

                for (Size component_index = 0; component_index < component_array->GetSize(); ++component_index)
                {
                    const ecs::Entity entity = component_array->GetEntity(component_index);
                    auto entity_index_it = entity_to_index.find(entity);
                    if (entity_index_it == entity_to_index.end())
                    {
                        continue;
                    }

                    component_entities.push_back(entity);
                    item_entity_indices.push_back(entity_index_it->second);
                }

                if (component_entities.empty())
                {
                    continue;
                }

                std::ostringstream type_stream;
                type_stream << "0x" << std::hex << std::uppercase << type_desc->type_id;
                const String type_key = type_stream.str();
                archive.BeginObject(type_key.c_str());

                String type_name = type_desc->name ? type_desc->name : "";
                archive.Field("type", type_name); // might be removed?

                archive.BeginArray("items");
                for (uint64 entity_index : item_entity_indices)
                {
                    archive.Item(entity_index);
                }
                archive.EndArray();

                archive.BeginObject("fields");
                for (uint32 field_index = 0; type_desc->fields && field_index < type_desc->field_count; ++field_index)
                {
                    const won::FieldDesc& field = type_desc->fields[field_index];
                    if (field.struct_size < sizeof(won::FieldDesc) || field.field_id == 0 || (field.flags & won::FieldFlagSerializable) == 0)
                    {
                        continue;
                    }
                    if (field.offset > type_desc->size || field.size > type_desc->size - field.offset)
                    {
                        continue;
                    }

                    std::ostringstream field_stream;
                    field_stream << "0x" << std::hex << std::uppercase << field.field_id;
                    const String field_key = field_stream.str();
                    archive.BeginObject(field_key.c_str());

                    String field_name = field.name ? field.name : "";
                    archive.Field("name", field_name);  // might be removed?
                    archive.BeginArray("values");

                    for (ecs::Entity entity : component_entities)
                    {
                        const void* component = scene.GetComponent(entity, type_desc->type_id);
                        if (!component)
                        {
                            continue;
                        }

                        const void* field_value = static_cast<const uint8*>(component) + field.offset;
                        const bool hierarchy_parent_field = type_desc->type_id == reflection::TypeMeta<ecs::HierarchyComponent>::type_id && field.name && std::strcmp(field.name, "parent_id") == 0;
                        if (hierarchy_parent_field)
                        {
                            const ecs::Entity parent = *static_cast<const ecs::Entity*>(field_value);
                            auto parent_index_it = entity_to_index.find(parent);
                            uint64 parent_index = parent_index_it != entity_to_index.end() ? parent_index_it->second : invalid_entity_index;
                            archive.Item(parent_index);
                            continue;
                        }

                        switch (field.value_type)
                        {
                        case won::ValueType::Bool: { bool value = *static_cast<const bool*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Int8: { int8 value = *static_cast<const int8*>(field_value); archive.Item(value); break; }
                        case won::ValueType::UInt8: { uint8 value = *static_cast<const uint8*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Int16: { int16 value = *static_cast<const int16*>(field_value); archive.Item(value); break; }
                        case won::ValueType::UInt16: { uint16 value = *static_cast<const uint16*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Int32: { int32 value = *static_cast<const int32*>(field_value); archive.Item(value); break; }
                        case won::ValueType::UInt32: { uint32 value = *static_cast<const uint32*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Int64: { int64 value = *static_cast<const int64*>(field_value); archive.Item(value); break; }
                        case won::ValueType::UInt64: { uint64 value = *static_cast<const uint64*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Float32: { float value = *static_cast<const float*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Float64: { double value = *static_cast<const double*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Int32x2: { int2 value = *static_cast<const int2*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Int32x3: { int3 value = *static_cast<const int3*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Int32x4: { int4 value = *static_cast<const int4*>(field_value); archive.Item(value); break; }
                        case won::ValueType::UInt32x2: { uint2 value = *static_cast<const uint2*>(field_value); archive.Item(value); break; }
                        case won::ValueType::UInt32x3: { uint3 value = *static_cast<const uint3*>(field_value); archive.Item(value); break; }
                        case won::ValueType::UInt32x4: { uint4 value = *static_cast<const uint4*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Float32x2: { float2 value = *static_cast<const float2*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Float32x3: { float3 value = *static_cast<const float3*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Float32x4: { float4 value = *static_cast<const float4*>(field_value); archive.Item(value); break; }
                        case won::ValueType::String: { String value = *static_cast<const String*>(field_value); archive.Item(value); break; }
                        case won::ValueType::Enum:
                        {
                            int64 value = 0;
                            switch (field.size)
                            {
                            case 1: value = *static_cast<const int8*>(field_value); break;
                            case 2: value = *static_cast<const int16*>(field_value); break;
                            case 4: value = *static_cast<const int32*>(field_value); break;
                            case 8: value = *static_cast<const int64*>(field_value); break;
                            default: break;
                            }
                            archive.Item(value);
                            break;
                        }
                        default:
                            break;
                        }
                    }

                    archive.EndArray();
                    archive.EndObject();
                }
                archive.EndObject();
                archive.EndObject();
            }
            archive.EndObject();
            archive.EndObject();
        }

        void ReadScene(JsonArchive& archive, ecs::Scene& scene)
        {
            if (!archive.BeginObject())
            {
                return;
            }

            uint32 version = 0;
            archive.Field("version", version);
            scene.ClearEntities();

            Vector<ecs::Entity> entities;
            uint64 entity_count = 0;
            archive.Field("entity_count", entity_count);
            entities.reserve(static_cast<Size>(entity_count));
            for (uint64 i = 0; i < entity_count; ++i)
            {
                entities.push_back(scene.CreateEntity());
            }

            if (archive.BeginObject("components"))
            {
                Vector<String> type_keys = archive.GetObjectKeys();
                for (const String& type_key : type_keys)
                {
                    if (!archive.BeginObject(type_key.c_str()))
                    {
                        continue;
                    }

                    String type_name;
                    archive.Field("type", type_name);  // might be removed?
                    const won::TypeId type_id = static_cast<won::TypeId>(std::strtoull(type_key.c_str(), nullptr, 0));
                    const won::TypeDesc* type_desc = reflection::FindType(type_id);
                    if (!type_desc && !type_name.empty())
                    {
                        type_desc = reflection::FindType(type_name);
                    }

                    Vector<ecs::Entity> component_entities;
                    if (archive.BeginArray("items"))
                    {
                        const Size item_count = archive.GetArraySize();
                        component_entities.reserve(item_count);
                        for (Size i = 0; i < item_count; ++i)
                        {
                            uint64 entity_index = 0;
                            archive.Item(entity_index);
                            ecs::Entity entity = entity_index < entities.size() ? entities[static_cast<Size>(entity_index)] : ecs::INVALID_ENTITY;
                            component_entities.push_back(entity);
                            if (type_desc && entity != ecs::INVALID_ENTITY)
                            {
                                scene.AddComponent(entity, type_desc);
                            }
                        }
                        archive.EndArray();
                    }

                    if (!type_desc)
                    {
                        backlog::Post("Scene load skipped missing component type: " + type_key, backlog::LogLevel::Warning);
                        archive.EndObject();
                        continue;
                    }

                    if (archive.BeginObject("fields"))
                    {
                        Vector<String> field_keys = archive.GetObjectKeys();
                        for (const String& field_key : field_keys)
                        {
                            if (!archive.BeginObject(field_key.c_str()))
                            {
                                continue;
                            }

                            String field_name;
                            archive.Field("name", field_name);  // might be removed?
                            const won::FieldId field_id = static_cast<won::FieldId>(std::strtoull(field_key.c_str(), nullptr, 0));
                            const won::FieldDesc* field = nullptr;

                            for (uint32 field_index = 0; type_desc->fields && field_index < type_desc->field_count; ++field_index)
                            {
                                const won::FieldDesc& candidate = type_desc->fields[field_index];
                                if (candidate.struct_size < sizeof(won::FieldDesc) || candidate.field_id == 0 || (candidate.flags & won::FieldFlagSerializable) == 0)
                                {
                                    continue;
                                }
                                if (candidate.field_id == field_id)
                                {
                                    field = &candidate;
                                    break;
                                }
                            }

                            if (archive.BeginArray("values"))
                            {
                                const Size value_count = archive.GetArraySize();
                                for (Size value_index = 0; value_index < value_count; ++value_index)
                                {
                                    if (!archive.BeginItem())
                                    {
                                        continue;
                                    }

                                    if (field && value_index < component_entities.size())
                                    {
                                        const ecs::Entity entity = component_entities[value_index];
                                        void* component = scene.GetComponent(entity, type_desc->type_id);
                                        if (component && field->offset <= type_desc->size && field->size <= type_desc->size - field->offset)
                                        {
                                            void* field_value = static_cast<uint8*>(component) + field->offset;
                                            const bool hierarchy_parent_field = type_desc->type_id == reflection::TypeMeta<ecs::HierarchyComponent>::type_id && field->name && std::strcmp(field->name, "parent_id") == 0;
                                            if (hierarchy_parent_field)
                                            {
                                                uint64 parent_index = invalid_entity_index;
                                                archive.Value(parent_index);
                                                *static_cast<ecs::Entity*>(field_value) = parent_index < entities.size() ? entities[static_cast<Size>(parent_index)] : ecs::INVALID_ENTITY;
                                            }
                                            else
                                            {
                                                switch (field->value_type)
                                                {
                                                case won::ValueType::Bool: archive.Value(*static_cast<bool*>(field_value)); break;
                                                case won::ValueType::Int8: archive.Value(*static_cast<int8*>(field_value)); break;
                                                case won::ValueType::UInt8: archive.Value(*static_cast<uint8*>(field_value)); break;
                                                case won::ValueType::Int16: archive.Value(*static_cast<int16*>(field_value)); break;
                                                case won::ValueType::UInt16: archive.Value(*static_cast<uint16*>(field_value)); break;
                                                case won::ValueType::Int32: archive.Value(*static_cast<int32*>(field_value)); break;
                                                case won::ValueType::UInt32: archive.Value(*static_cast<uint32*>(field_value)); break;
                                                case won::ValueType::Int64: archive.Value(*static_cast<int64*>(field_value)); break;
                                                case won::ValueType::UInt64: archive.Value(*static_cast<uint64*>(field_value)); break;
                                                case won::ValueType::Float32: archive.Value(*static_cast<float*>(field_value)); break;
                                                case won::ValueType::Float64: archive.Value(*static_cast<double*>(field_value)); break;
                                                case won::ValueType::Int32x2: Serialize(archive, *static_cast<int2*>(field_value)); break;
                                                case won::ValueType::Int32x3: Serialize(archive, *static_cast<int3*>(field_value)); break;
                                                case won::ValueType::Int32x4: Serialize(archive, *static_cast<int4*>(field_value)); break;
                                                case won::ValueType::UInt32x2: Serialize(archive, *static_cast<uint2*>(field_value)); break;
                                                case won::ValueType::UInt32x3: Serialize(archive, *static_cast<uint3*>(field_value)); break;
                                                case won::ValueType::UInt32x4: Serialize(archive, *static_cast<uint4*>(field_value)); break;
                                                case won::ValueType::Float32x2: Serialize(archive, *static_cast<float2*>(field_value)); break;
                                                case won::ValueType::Float32x3: Serialize(archive, *static_cast<float3*>(field_value)); break;
                                                case won::ValueType::Float32x4: Serialize(archive, *static_cast<float4*>(field_value)); break;
                                                case won::ValueType::String: archive.Value(*static_cast<String*>(field_value)); break;
                                                case won::ValueType::Enum:
                                                {
                                                    int64 value = 0;
                                                    archive.Value(value);
                                                    switch (field->size)
                                                    {
                                                    case 1: *static_cast<int8*>(field_value) = static_cast<int8>(value); break;
                                                    case 2: *static_cast<int16*>(field_value) = static_cast<int16>(value); break;
                                                    case 4: *static_cast<int32*>(field_value) = static_cast<int32>(value); break;
                                                    case 8: *static_cast<int64*>(field_value) = static_cast<int64>(value); break;
                                                    default: break;
                                                    }
                                                    break;
                                                }
                                                default:
                                                    break;
                                                }
                                            }
                                        }
                                    }

                                    archive.EndItem();
                                }
                                archive.EndArray();
                            }
                            archive.EndObject();
                        }
                        archive.EndObject();
                    }

                    archive.EndObject();
                }
                archive.EndObject();
            }

            for (ecs::Entity entity : scene.GetEntities())
            {
                if (ecs::TransformComponent* transform = scene.GetComponent<ecs::TransformComponent>(entity))
                {
                    transform->SetDirty();
                }
                if (ecs::GeometryComponent* geometry = scene.GetComponent<ecs::GeometryComponent>(entity))
                {
                    geometry->UpdateLocalBounds();
                    geometry->SetDirty();
                }
                if (ecs::Sprite2DComponent* sprite = scene.GetComponent<ecs::Sprite2DComponent>(entity))
                {
                    sprite->SetDirty();
                }
                if (ecs::Sprite3DComponent* sprite = scene.GetComponent<ecs::Sprite3DComponent>(entity))
                {
                    sprite->SetDirty();
                }
                if (ecs::Text2DComponent* text = scene.GetComponent<ecs::Text2DComponent>(entity))
                {
                    text->SetDirty();
                }
                if (ecs::Text3DComponent* text = scene.GetComponent<ecs::Text3DComponent>(entity))
                {
                    text->SetDirty();
                }
            }
            scene.SetBVHDirty();
            archive.EndObject();
        }
    }

    void Serialize(JsonArchive& archive, ecs::Scene& scene, const SceneSerializeDesc& desc)
    {
        if (archive.IsReadMode())
        {
            ReadScene(archive, scene);
        }
        else
        {
            WriteScene(archive, scene, desc);
        }
    }

    void Serialize(JsonArchive& archive, const ecs::Scene& scene, const SceneSerializeDesc& desc)
    {
        if (archive.IsWriteMode())
        {
            WriteScene(archive, scene, desc);
        }
    }
}
