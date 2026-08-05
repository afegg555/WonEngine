#include "SceneSerializer.h"
#include "Backlog.h"
#include "Reflection.h"
#include "Scene.h"

#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <sstream>

namespace won::serialize
{
    namespace
    {
        constexpr uint64 invalid_entity_index = static_cast<uint64>(-1);
        constexpr uint32 invalid_resource_index = static_cast<uint32>(-1);

        enum class EntityRefEncoding : uint32
        {
            FileIndex, // entity references are encoded as indices into the serialized entity array
            RawId, // entity references are encoded as raw entity IDs (uint64)
        };

        struct EntityRefContext
        {
            EntityRefEncoding encoding = EntityRefEncoding::FileIndex;
            const UnorderedMap<ecs::Entity, uint64>* entity_to_index = nullptr;
            const Vector<ecs::Entity>* entities = nullptr;
        };

        bool SerializeReflectedValue(JsonArchive& archive, won::ValueType value_type, uint32 value_size, void* value)
        {
            switch (value_type)
            {
            case won::ValueType::Bool: Serialize(archive, *static_cast<bool*>(value)); return true;
            case won::ValueType::Int8: Serialize(archive, *static_cast<int8*>(value)); return true;
            case won::ValueType::UInt8: Serialize(archive, *static_cast<uint8*>(value)); return true;
            case won::ValueType::Int16: Serialize(archive, *static_cast<int16*>(value)); return true;
            case won::ValueType::UInt16: Serialize(archive, *static_cast<uint16*>(value)); return true;
            case won::ValueType::Int32: Serialize(archive, *static_cast<int32*>(value)); return true;
            case won::ValueType::UInt32: Serialize(archive, *static_cast<uint32*>(value)); return true;
            case won::ValueType::Int64: Serialize(archive, *static_cast<int64*>(value)); return true;
            case won::ValueType::UInt64: Serialize(archive, *static_cast<uint64*>(value)); return true;
            case won::ValueType::Float32: Serialize(archive, *static_cast<float*>(value)); return true;
            case won::ValueType::Float64: Serialize(archive, *static_cast<double*>(value)); return true;
            case won::ValueType::Int32x2: Serialize(archive, *static_cast<int2*>(value)); return true;
            case won::ValueType::Int32x3: Serialize(archive, *static_cast<int3*>(value)); return true;
            case won::ValueType::Int32x4: Serialize(archive, *static_cast<int4*>(value)); return true;
            case won::ValueType::UInt32x2: Serialize(archive, *static_cast<uint2*>(value)); return true;
            case won::ValueType::UInt32x3: Serialize(archive, *static_cast<uint3*>(value)); return true;
            case won::ValueType::UInt32x4: Serialize(archive, *static_cast<uint4*>(value)); return true;
            case won::ValueType::Float32x2: Serialize(archive, *static_cast<float2*>(value)); return true;
            case won::ValueType::Float32x3: Serialize(archive, *static_cast<float3*>(value)); return true;
            case won::ValueType::Float32x4: Serialize(archive, *static_cast<float4*>(value)); return true;
            case won::ValueType::String: Serialize(archive, *static_cast<String*>(value)); return true;
            case won::ValueType::Enum:
            {
                int64 copy = 0;
                switch (value_size)
                {
                case 1: copy = *static_cast<int8*>(value); break;
                case 2: copy = *static_cast<int16*>(value); break;
                case 4: copy = *static_cast<int32*>(value); break;
                case 8: copy = *static_cast<int64*>(value); break;
                default: break;
                }
                Serialize(archive, copy);
                if (archive.IsReadMode())
                {
                    switch (value_size)
                    {
                    case 1: *static_cast<int8*>(value) = static_cast<int8>(copy); break;
                    case 2: *static_cast<int16*>(value) = static_cast<int16>(copy); break;
                    case 4: *static_cast<int32*>(value) = static_cast<int32>(copy); break;
                    case 8: *static_cast<int64*>(value) = static_cast<int64>(copy); break;
                    default: break;
                    }
                }
                return true;
            }
            default:
                return false;
            }
        }

        void WriteReflectedData(JsonArchive& archive, won::ValueType value_type, const won::TypeDesc* type_desc, uint32 value_size, const won::ArrayDesc* array_desc, const void* value, const EntityRefContext* entity_refs = nullptr)
        {
            if (!value || SerializeReflectedValue(archive, value_type, value_size, const_cast<void*>(value)))
            {
                return;
            }

            switch (value_type)
            {
            case won::ValueType::CustomStruct:
            {
                if (!type_desc)
                {
                    break;
                }

                archive.BeginObject();
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
                    const void* field_value = static_cast<const uint8*>(value) + field.offset;
                    if (archive.BeginField(field_key.c_str()))
                    {
                        if ((field.flags & won::FieldFlagEntityRef) != 0 && entity_refs)
                        {
                            const ecs::Entity referenced = *static_cast<const ecs::Entity*>(field_value);
                            uint64 encoded = referenced;
                            if (entity_refs->encoding == EntityRefEncoding::FileIndex)
                            {
                                encoded = invalid_entity_index;
                                if (entity_refs->entity_to_index)
                                {
                                    auto referenced_index_it = entity_refs->entity_to_index->find(referenced);
                                    if (referenced_index_it != entity_refs->entity_to_index->end())
                                    {
                                        encoded = referenced_index_it->second;
                                    }
                                }
                            }
                            archive.Value(encoded);
                        }
                        else
                        {
                            WriteReflectedData(archive, field.value_type, nullptr, field.size, field.array_desc, field_value, entity_refs);
                        }
                        archive.EndField();
                    }
                }
                archive.EndObject();
                break;
            }
            case won::ValueType::Array:
            {
                archive.BeginArray();
                if (!array_desc || array_desc->struct_size < sizeof(won::ArrayDesc) || !array_desc->GetSize || !array_desc->GetConstElement)
                {
                    archive.EndArray();
                    break;
                }

                const won::TypeDesc* element_type = reflection::FindType(array_desc->element_type_id);
                if (!element_type)
                {
                    archive.EndArray();
                    break;
                }

                const uint32 count = array_desc->GetSize(value);
                for (uint32 index = 0; index < count; ++index)
                {
                    const void* element = array_desc->GetConstElement(value, index);
                    archive.BeginItem();
                    WriteReflectedData(archive, element_type->value_type, element_type, element_type->size, nullptr, element, entity_refs);
                    archive.EndItem();
                }
                archive.EndArray();
                break;
            }
            default:
                break;
            }
        }

        void ReadReflectedData(JsonArchive& archive, won::ValueType value_type, const won::TypeDesc* type_desc, uint32 value_size, const won::ArrayDesc* array_desc, void* value, const EntityRefContext* entity_refs = nullptr)
        {
            if (!value || SerializeReflectedValue(archive, value_type, value_size, value))
            {
                return;
            }

            switch (value_type)
            {
            case won::ValueType::CustomStruct:
            {
                if (!type_desc || !archive.BeginObject())
                {
                    break;
                }

                Vector<String> field_keys = archive.GetObjectKeys();
                for (const String& field_key : field_keys)
                {
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

                    if (!field || field->offset > type_desc->size || field->size > type_desc->size - field->offset)
                    {
                        continue;
                    }

                    void* field_value = static_cast<uint8*>(value) + field->offset;
                    if (archive.BeginField(field_key.c_str()))
                    {
                        if ((field->flags & won::FieldFlagEntityRef) != 0 && entity_refs)
                        {
                            uint64 encoded = invalid_entity_index;
                            archive.Value(encoded);
                            if (entity_refs->encoding == EntityRefEncoding::RawId)
                            {
                                *static_cast<ecs::Entity*>(field_value) = static_cast<ecs::Entity>(encoded);
                            }
                            else
                            {
                                const Vector<ecs::Entity>* entities = entity_refs->entities;
                                *static_cast<ecs::Entity*>(field_value) = (entities && encoded < entities->size()) ? (*entities)[static_cast<Size>(encoded)] : ecs::INVALID_ENTITY;
                            }
                        }
                        else
                        {
                            ReadReflectedData(archive, field->value_type, nullptr, field->size, field->array_desc, field_value, entity_refs);
                        }
                        archive.EndField();
                    }
                }
                archive.EndObject();
                break;
            }
            case won::ValueType::Array:
            {
                if (!array_desc || array_desc->struct_size < sizeof(won::ArrayDesc) || !array_desc->GetElement || !archive.BeginArray())
                {
                    break;
                }

                const won::TypeDesc* element_type = reflection::FindType(array_desc->element_type_id);
                if (!element_type)
                {
                    archive.EndArray();
                    break;
                }

                const Size array_size = archive.GetArraySize();
                if (array_desc->Resize)
                {
                    array_desc->Resize(value, static_cast<uint32>(array_size));
                }
                const Size count = array_desc->GetSize ? (std::min)(array_size, static_cast<Size>(array_desc->GetSize(value))) : array_size;

                for (Size index = 0; index < array_size; ++index)
                {
                    if (!archive.BeginItem())
                    {
                        continue;
                    }

                    if (index < count)
                    {
                        void* element = array_desc->GetElement(value, static_cast<uint32>(index));
                        ReadReflectedData(archive, element_type->value_type, element_type, element_type->size, nullptr, element, entity_refs);
                    }

                    archive.EndItem();
                }
                archive.EndArray();
                break;
            }
            default:
                break;
            }
        }

        Vector<ecs::Entity> CollectSubtree(ecs::Scene& scene, ecs::Entity root)
        {
            Vector<ecs::Entity> subtree;
            subtree.push_back(root);
            auto hierarchy_array = scene.GetComponentArray<ecs::HierarchyComponent>();
            if (hierarchy_array)
            {
                for (Size head = 0; head < subtree.size(); ++head)
                {
                    const ecs::Entity parent = subtree[head];
                    for (Size i = 0; i < hierarchy_array->GetSize(); ++i)
                    {
                        if (hierarchy_array->data[i].parent_id == parent)
                        {
                            subtree.push_back(hierarchy_array->index_to_entity[i]);
                        }
                    }
                }
            }
            return subtree;
        }

        void WriteEntities(JsonArchive& archive, const ecs::Scene& scene, const SaveSceneDesc& desc, const Vector<ecs::Entity>* ordered_entities = nullptr, bool write_entity_ids = false, const won::TypeId* type_filter = nullptr, EntityRefEncoding entity_ref_encoding = EntityRefEncoding::FileIndex)
        {
            archive.BeginObject();
            uint32 version = scene_format_version;
            archive.Field("version", version);
            if (entity_ref_encoding != EntityRefEncoding::FileIndex)
            {
                uint32 entity_ref_encoding_value = static_cast<uint32>(entity_ref_encoding);
                archive.Field("entity_ref_encoding", entity_ref_encoding_value);
            }

            UnorderedMap<ecs::Entity, bool> excluded_entity_map;
            if (desc.excluded_entities)
            {
                excluded_entity_map.reserve(desc.excluded_entities->size());
                for (ecs::Entity entity : *desc.excluded_entities)
                {
                    excluded_entity_map[entity] = true;
                }
            }

            Vector<String> mesh_resources;
            Vector<String> texture_resources;
            Vector<String> font_resources;
            Vector<String> material_resources;
            UnorderedMap<String, uint32> mesh_resource_indices;
            UnorderedMap<String, uint32> texture_resource_indices;
            UnorderedMap<String, uint32> font_resource_indices;
            UnorderedMap<String, uint32> material_resource_indices;

            UnorderedMap<ecs::Entity, uint64> entity_to_index;
            uint64 entity_count = 0;
            if (ordered_entities)
            {
                for (ecs::Entity entity : *ordered_entities)
                {
                    entity_to_index[entity] = entity_count;
                    ++entity_count;
                }
            }
            else
            {
                for (ecs::Entity entity : scene.GetEntities())
                {
                    if (excluded_entity_map.find(entity) != excluded_entity_map.end())
                    {
                        continue;
                    }
                    entity_to_index[entity] = entity_count;
                    ++entity_count;
                }
            }
            archive.Field("entity_count", entity_count);

            if (write_entity_ids)
            {
                Vector<ecs::Entity> ordered_ids(static_cast<Size>(entity_count), ecs::INVALID_ENTITY);
                for (const auto& pair : entity_to_index)
                {
                    ordered_ids[static_cast<Size>(pair.second)] = pair.first;
                }

                archive.BeginArray("entity_ids");
                for (ecs::Entity entity : ordered_ids)
                {
                    uint64 id_value = entity;
                    archive.Item(id_value);
                }
                archive.EndArray();
            }

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
                if (type_filter && type_desc->type_id != *type_filter)
                {
                    continue;
                }

                std::shared_ptr<const ecs::IComponentArray> component_array = scene.GetComponentArray(type_desc->type_id);
                if (!component_array || component_array->GetSize() == 0)
                {
                    continue;
                }

                Vector<ecs::Entity> component_entities;
                Vector<const void*> component_pointers;
                component_entities.reserve(component_array->GetSize());
                component_pointers.reserve(component_array->GetSize());

                for (Size component_index = 0; component_index < component_array->GetSize(); ++component_index)
                {
                    const ecs::Entity entity = component_array->GetEntity(component_index);
                    if (entity_to_index.find(entity) == entity_to_index.end())
                    {
                        continue;
                    }

                    const void* component = component_array->GetRawData(entity);
                    if (!component)
                    {
                        continue;
                    }

                    component_entities.push_back(entity);
                    component_pointers.push_back(component);
                }

                if (component_entities.empty())
                {
                    continue;
                }

                std::ostringstream type_stream;
                type_stream << "0x" << std::hex << std::uppercase << type_desc->type_id;
                const String type_key = type_stream.str();
                archive.BeginObject(type_key.c_str());

                // String type_name = type_desc->name ? type_desc->name : "";
                // archive.Field("type", type_name); // might be removed?

                archive.BeginArray("items");
                for (ecs::Entity entity : component_entities)
                {
                    auto entity_index_it = entity_to_index.find(entity);
                    if (entity_index_it != entity_to_index.end())
                    {
                        archive.Item(entity_index_it->second);
                    }
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

                    // String field_name = field.name ? field.name : "";
                    // archive.Field("name", field_name);  // might be removed?
                    archive.BeginArray("values");

                    for (const void* component : component_pointers)
                    {
                        const void* field_value = static_cast<const uint8*>(component) + field.offset;
                        if ((field.flags & FieldFlagEntityRef) != 0)
                        {
                            const ecs::Entity referenced = *static_cast<const ecs::Entity*>(field_value);
                            if (entity_ref_encoding == EntityRefEncoding::RawId)
                            {
                                uint64 raw_id = referenced;
                                archive.Item(raw_id);
                            }
                            else
                            {
                                auto referenced_index_it = entity_to_index.find(referenced);
                                uint64 referenced_index = referenced_index_it != entity_to_index.end() ? referenced_index_it->second : invalid_entity_index;
                                archive.Item(referenced_index);
                            }
                            continue;
                        }

                        archive.BeginItem();
                        const bool geometry_mesh_field = type_desc->type_id == reflection::TypeMeta<ecs::GeometryComponent>::type_id && field.name && std::strcmp(field.name, "mesh_asset_path") == 0;
                        const bool text_font_field = (type_desc->type_id == reflection::TypeMeta<ecs::Text2DComponent>::type_id || type_desc->type_id == reflection::TypeMeta<ecs::Text3DComponent>::type_id) && field.name && std::strcmp(field.name, "font_asset_path") == 0;
                        const bool material_path_field = type_desc->type_id == reflection::TypeMeta<ecs::MaterialComponent>::type_id && field.name && std::strcmp(field.name, "material_asset_path") == 0;
                        if (geometry_mesh_field)
                        {
                            const String& path = *static_cast<const String*>(field_value);
                            uint32 resource_index = invalid_resource_index;
                            if (!path.empty())
                            {
                                auto resource_it = mesh_resource_indices.find(path);
                                if (resource_it != mesh_resource_indices.end())
                                {
                                    resource_index = resource_it->second;
                                }
                                else
                                {
                                    resource_index = static_cast<uint32>(mesh_resources.size());
                                    mesh_resources.push_back(path);
                                    mesh_resource_indices[path] = resource_index;
                                }
                            }
                            archive.Value(resource_index);
                        }
                        else if (text_font_field)
                        {
                            const String& path = *static_cast<const String*>(field_value);
                            uint32 resource_index = invalid_resource_index;
                            if (!path.empty())
                            {
                                auto resource_it = font_resource_indices.find(path);
                                if (resource_it != font_resource_indices.end())
                                {
                                    resource_index = resource_it->second;
                                }
                                else
                                {
                                    resource_index = static_cast<uint32>(font_resources.size());
                                    font_resources.push_back(path);
                                    font_resource_indices[path] = resource_index;
                                }
                            }
                            archive.Value(resource_index);
                        }
                        else if (material_path_field)
                        {
                            const ecs::MaterialComponent* material_comp = static_cast<const ecs::MaterialComponent*>(component);
                            const won::TypeDesc* material_slot_type = reflection::TypeMeta<resource::MaterialSlot>::Get();
                            const won::TypeDesc* texture_map_type = reflection::TypeMeta<resource::MaterialSlot::TextureMap>::Get();

                            uint32 material_index = invalid_resource_index;
                            if (!material_comp->material_asset_path.empty())
                            {
                                const String& path = material_comp->material_asset_path;
                                auto resource_it = material_resource_indices.find(path);
                                if (resource_it != material_resource_indices.end())
                                {
                                    material_index = resource_it->second;
                                }
                                else
                                {
                                    material_index = static_cast<uint32>(material_resources.size());
                                    material_resources.push_back(path);
                                    material_resource_indices[path] = material_index;
                                }
                            }

                            archive.BeginObject();
                            archive.Field("ref", material_index);
							if (material_index == invalid_resource_index && material_comp->material && archive.BeginField("slots")) // forked material
                            {
                            archive.BeginArray();
                            for (const resource::MaterialSlot& material_slot : material_comp->material->slots)
                            {
                                archive.BeginItem();
                                archive.BeginObject();
                                for (uint32 slot_field_index = 0; material_slot_type && material_slot_type->fields && slot_field_index < material_slot_type->field_count; ++slot_field_index)
                                {
                                    const won::FieldDesc& slot_field = material_slot_type->fields[slot_field_index];
                                    if (slot_field.struct_size < sizeof(won::FieldDesc) || slot_field.field_id == 0 || (slot_field.flags & won::FieldFlagSerializable) == 0)
                                    {
                                        continue;
                                    }
                                    if (slot_field.offset > material_slot_type->size || slot_field.size > material_slot_type->size - slot_field.offset)
                                    {
                                        continue;
                                    }

                                    std::ostringstream slot_field_stream;
                                    slot_field_stream << "0x" << std::hex << std::uppercase << slot_field.field_id;
                                    const String slot_field_key = slot_field_stream.str();
                                    const void* slot_field_value = static_cast<const uint8*>(static_cast<const void*>(&material_slot)) + slot_field.offset;
                                    if (slot_field.name && std::strcmp(slot_field.name, "textures") == 0)
                                    {
                                        if (archive.BeginField(slot_field_key.c_str()))
                                        {
                                            archive.BeginArray();
                                            for (const resource::MaterialSlot::TextureMap& texture_map : material_slot.textures)
                                            {
                                                archive.BeginItem();
                                                archive.BeginObject();
                                                for (uint32 texture_field_index = 0; texture_map_type && texture_map_type->fields && texture_field_index < texture_map_type->field_count; ++texture_field_index)
                                                {
                                                    const won::FieldDesc& texture_field = texture_map_type->fields[texture_field_index];
                                                    if (texture_field.struct_size < sizeof(won::FieldDesc) || texture_field.field_id == 0 || (texture_field.flags & won::FieldFlagSerializable) == 0)
                                                    {
                                                        continue;
                                                    }
                                                    if (texture_field.offset > texture_map_type->size || texture_field.size > texture_map_type->size - texture_field.offset)
                                                    {
                                                        continue;
                                                    }

                                                    std::ostringstream texture_field_stream;
                                                    texture_field_stream << "0x" << std::hex << std::uppercase << texture_field.field_id;
                                                    const String texture_field_key = texture_field_stream.str();
                                                    const void* texture_field_value = static_cast<const uint8*>(static_cast<const void*>(&texture_map)) + texture_field.offset;
                                                    if (archive.BeginField(texture_field_key.c_str()))
                                                    {
                                                        if (texture_field.name && std::strcmp(texture_field.name, "texture_asset_path") == 0)
                                                        {
                                                            const String& path = *static_cast<const String*>(texture_field_value);
                                                            uint32 resource_index = invalid_resource_index;
                                                            if (!path.empty())
                                                            {
                                                                auto resource_it = texture_resource_indices.find(path);
                                                                if (resource_it != texture_resource_indices.end())
                                                                {
                                                                    resource_index = resource_it->second;
                                                                }
                                                                else
                                                                {
                                                                    resource_index = static_cast<uint32>(texture_resources.size());
                                                                    texture_resources.push_back(path);
                                                                    texture_resource_indices[path] = resource_index;
                                                                }
                                                            }
                                                            archive.Value(resource_index);
                                                        }
                                                        else
                                                        {
                                                            WriteReflectedData(archive, texture_field.value_type, nullptr, texture_field.size, texture_field.array_desc, texture_field_value);
                                                        }
                                                        archive.EndField();
                                                    }
                                                }
                                                archive.EndObject();
                                                archive.EndItem();
                                            }
                                            archive.EndArray();
                                            archive.EndField();
                                        }
                                    }
                                    else if (archive.BeginField(slot_field_key.c_str()))
                                    {
                                        WriteReflectedData(archive, slot_field.value_type, nullptr, slot_field.size, slot_field.array_desc, slot_field_value);
                                        archive.EndField();
                                    }
                                }
                                archive.EndObject();
                                archive.EndItem();
                            }
                            archive.EndArray();
                            archive.EndField();
                            }
                            archive.EndObject();
                        }
                        else
                        {
                            EntityRefContext entity_refs = {};
                            entity_refs.encoding = entity_ref_encoding;
                            entity_refs.entity_to_index = &entity_to_index;
                            WriteReflectedData(archive, field.value_type, nullptr, field.size, field.array_desc, field_value, &entity_refs);
                        }
                        archive.EndItem();
                    }

                    archive.EndArray();
                    archive.EndObject();
                }
                archive.EndObject();
                archive.EndObject();
            }
            archive.EndObject();

            archive.BeginObject("resources");
            archive.BeginArray("meshes");
            for (const String& path : mesh_resources)
            {
                archive.Item(path);
            }
            archive.EndArray();
            archive.BeginArray("textures");
            for (const String& path : texture_resources)
            {
                archive.Item(path);
            }
            archive.EndArray();
            archive.BeginArray("fonts");
            for (const String& path : font_resources)
            {
                archive.Item(path);
            }
            archive.EndArray();
            archive.BeginArray("materials");
            for (const String& path : material_resources)
            {
                archive.Item(path);
            }
            archive.EndArray();
            archive.EndObject();

            archive.EndObject();
        }

        bool ReadEntities(JsonArchive& archive, ecs::Scene& scene, Vector<ecs::Entity>& entities, ecs::Entity preallocated_root = ecs::INVALID_ENTITY, bool preserve_entity_ids = false)
        {
            if (!archive.BeginObject())
            {
                return false;
            }

            uint32 version = 0;
            archive.Field("version", version);
            if (version > scene_format_version)
            {
                wonlog_warning("Scene format version mismatch: file=%u runtime=%u", static_cast<unsigned>(version), static_cast<unsigned>(scene_format_version));
                return false;
            }

            uint32 entity_ref_encoding_value = static_cast<uint32>(EntityRefEncoding::FileIndex);
            archive.Field("entity_ref_encoding", entity_ref_encoding_value);
            const EntityRefEncoding entity_ref_encoding = static_cast<EntityRefEncoding>(entity_ref_encoding_value);

            Vector<String> mesh_resources;
            Vector<String> texture_resources;
            Vector<String> font_resources;
            Vector<String> material_resources;
            if (archive.BeginObject("resources"))
            {
                if (archive.BeginArray("meshes"))
                {
                    const Size count = archive.GetArraySize();
                    mesh_resources.reserve(count);
                    for (Size i = 0; i < count; ++i)
                    {
                        String path;
                        archive.Item(path);
                        mesh_resources.push_back(path);
                    }
                    archive.EndArray();
                }
                if (archive.BeginArray("textures"))
                {
                    const Size count = archive.GetArraySize();
                    texture_resources.reserve(count);
                    for (Size i = 0; i < count; ++i)
                    {
                        String path;
                        archive.Item(path);
                        texture_resources.push_back(path);
                    }
                    archive.EndArray();
                }
                if (archive.BeginArray("fonts"))
                {
                    const Size count = archive.GetArraySize();
                    font_resources.reserve(count);
                    for (Size i = 0; i < count; ++i)
                    {
                        String path;
                        archive.Item(path);
                        font_resources.push_back(path);
                    }
                    archive.EndArray();
                }
                if (archive.BeginArray("materials"))
                {
                    const Size count = archive.GetArraySize();
                    material_resources.reserve(count);
                    for (Size i = 0; i < count; ++i)
                    {
                        String path;
                        archive.Item(path);
                        material_resources.push_back(path);
                    }
                    archive.EndArray();
                }
                archive.EndObject();
            }
            uint64 entity_count = 0;
            archive.Field("entity_count", entity_count);
            entities.clear();
            entities.reserve(static_cast<Size>(entity_count));
            Vector<ecs::Entity> stored_ids;
            if (archive.BeginArray("entity_ids"))
            {
                const Size stored_count = archive.GetArraySize();
                stored_ids.reserve(stored_count);
                for (Size i = 0; i < stored_count; ++i)
                {
                    uint64 id_value = ecs::INVALID_ENTITY;
                    archive.Item(id_value);
                    stored_ids.push_back(static_cast<ecs::Entity>(id_value));
                }
                archive.EndArray();
            }

			assert(!preserve_entity_ids || preallocated_root == ecs::INVALID_ENTITY); // cannot preserve entity IDs and use a preallocated root at the same time

            if (preserve_entity_ids)
            {
                if (stored_ids.size() != static_cast<Size>(entity_count))
                {
                    assert(false && "preserve_entity_ids requested but stored entity ids are missing or mismatched");
                    wonlog_error("Scene load failed: preserve_entity_ids requested but stored ids (%zu) do not match entity count (%llu)", stored_ids.size(), static_cast<unsigned long long>(entity_count));
                    return false;
                }
                for (ecs::Entity stored_id : stored_ids)
                {
                    if (stored_id == ecs::INVALID_ENTITY || scene.IsEntityAlive(stored_id))
                    {
                        assert(false && "preserve_entity_ids requested but a stored id is invalid or still alive");
                        wonlog_error("Scene load failed: stored entity id %llu is invalid or still alive", static_cast<unsigned long long>(stored_id));
                        return false;
                    }
                }
            }

            for (uint64 i = 0; i < entity_count; ++i)
            {
                if (preserve_entity_ids)
                {
                    entities.push_back(scene.ReviveEntity(stored_ids[static_cast<Size>(i)]));
                }
                else if (i == 0 && preallocated_root != ecs::INVALID_ENTITY)
                {
                    entities.push_back(preallocated_root);
                }
                else
                {
                    entities.push_back(scene.CreateEntity());
                }
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

                    Vector<void*> component_pointers;
                    component_pointers.reserve(component_entities.size());
                    for (ecs::Entity entity : component_entities)
                    {
                        component_pointers.push_back(entity != ecs::INVALID_ENTITY ? scene.GetComponent(entity, type_desc->type_id) : nullptr);
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

                            //String field_name;
                            //archive.Field("name", field_name);  // might be removed?
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

                                    if (field && value_index < component_pointers.size())
                                    {
                                        void* component = component_pointers[value_index];
                                        if (component && field->offset <= type_desc->size && field->size <= type_desc->size - field->offset)
                                        {
                                            void* field_value = static_cast<uint8*>(component) + field->offset;
                                            if ((field->flags & FieldFlagEntityRef) != 0)
                                            {
                                                if (entity_ref_encoding == EntityRefEncoding::RawId)
                                                {
                                                    uint64 raw_id = ecs::INVALID_ENTITY;
                                                    archive.Value(raw_id);
                                                    *static_cast<ecs::Entity*>(field_value) = static_cast<ecs::Entity>(raw_id);
                                                }
                                                else
                                                {
                                                    uint64 referenced_index = invalid_entity_index;
                                                    archive.Value(referenced_index);
                                                    *static_cast<ecs::Entity*>(field_value) = referenced_index < entities.size() ? entities[static_cast<Size>(referenced_index)] : ecs::INVALID_ENTITY;
                                                }
                                            }
                                            else if (type_desc->type_id == reflection::TypeMeta<ecs::GeometryComponent>::type_id && field->name && std::strcmp(field->name, "mesh_asset_path") == 0)
                                            {
                                                uint32 resource_index = invalid_resource_index;
                                                archive.Value(resource_index);
                                                *static_cast<String*>(field_value) = resource_index < mesh_resources.size() ? mesh_resources[resource_index] : String();
                                            }
                                            else if ((type_desc->type_id == reflection::TypeMeta<ecs::Text2DComponent>::type_id || type_desc->type_id == reflection::TypeMeta<ecs::Text3DComponent>::type_id) && field->name && std::strcmp(field->name, "font_asset_path") == 0)
                                            {
                                                uint32 resource_index = invalid_resource_index;
                                                archive.Value(resource_index);
                                                *static_cast<String*>(field_value) = resource_index < font_resources.size() ? font_resources[resource_index] : String();
                                            }
                                            else if (type_desc->type_id == reflection::TypeMeta<ecs::MaterialComponent>::type_id && field->name && std::strcmp(field->name, "material_asset_path") == 0)
                                            {
                                                ecs::MaterialComponent* material_comp = static_cast<ecs::MaterialComponent*>(component);
                                                const won::TypeDesc* material_slot_type = reflection::TypeMeta<resource::MaterialSlot>::Get();
                                                const won::TypeDesc* texture_map_type = reflection::TypeMeta<resource::MaterialSlot::TextureMap>::Get();
                                                auto material = std::make_shared<resource::Material>();
                                                uint32 material_index = invalid_resource_index;
                                                archive.BeginObject();
                                                archive.Field("ref", material_index);
                                                if (material_index != invalid_resource_index)
                                                {
                                                    material_comp->material_asset_path = material_index < material_resources.size() ? material_resources[material_index] : String();
                                                }
                                                else if (archive.BeginField("slots") && archive.BeginArray())
                                                {
                                                    const Size slot_count = archive.GetArraySize();
                                                    material->slots.resize(slot_count);
                                                    for (Size slot_index = 0; slot_index < slot_count; ++slot_index)
                                                    {
                                                        if (!archive.BeginItem())
                                                        {
                                                            continue;
                                                        }

                                                        resource::MaterialSlot& material_slot = material->slots[slot_index];
                                                        if (archive.BeginObject())
                                                        {
                                                            Vector<String> slot_field_keys = archive.GetObjectKeys();
                                                            for (const String& slot_field_key : slot_field_keys)
                                                            {
                                                                const won::FieldId slot_field_id = static_cast<won::FieldId>(std::strtoull(slot_field_key.c_str(), nullptr, 0));
                                                                const won::FieldDesc* slot_field = nullptr;
                                                                for (uint32 slot_field_index = 0; material_slot_type && material_slot_type->fields && slot_field_index < material_slot_type->field_count; ++slot_field_index)
                                                                {
                                                                    const won::FieldDesc& candidate = material_slot_type->fields[slot_field_index];
                                                                    if (candidate.struct_size >= sizeof(won::FieldDesc) && candidate.field_id == slot_field_id && (candidate.flags & won::FieldFlagSerializable) != 0)
                                                                    {
                                                                        slot_field = &candidate;
                                                                        break;
                                                                    }
                                                                }
                                                                if (!slot_field || slot_field->offset > material_slot_type->size || slot_field->size > material_slot_type->size - slot_field->offset)
                                                                {
                                                                    continue;
                                                                }

                                                                void* slot_field_value = static_cast<uint8*>(static_cast<void*>(&material_slot)) + slot_field->offset;
                                                                if (!archive.BeginField(slot_field_key.c_str()))
                                                                {
                                                                    continue;
                                                                }

                                                                if (slot_field->name && std::strcmp(slot_field->name, "textures") == 0)
                                                                {
                                                                    if (archive.BeginArray())
                                                                    {
                                                                        const Size texture_count = archive.GetArraySize();
                                                                        for (Size texture_index = 0; texture_index < texture_count; ++texture_index)
                                                                        {
                                                                            if (!archive.BeginItem())
                                                                            {
                                                                                continue;
                                                                            }

                                                                            if (texture_index < TEXTURESLOT_COUNT && archive.BeginObject())
                                                                            {
                                                                                resource::MaterialSlot::TextureMap& texture_map = material_slot.textures[texture_index];
                                                                                Vector<String> texture_field_keys = archive.GetObjectKeys();
                                                                                for (const String& texture_field_key : texture_field_keys)
                                                                                {
                                                                                    const won::FieldId texture_field_id = static_cast<won::FieldId>(std::strtoull(texture_field_key.c_str(), nullptr, 0));
                                                                                    const won::FieldDesc* texture_field = nullptr;
                                                                                    for (uint32 texture_field_index = 0; texture_map_type && texture_map_type->fields && texture_field_index < texture_map_type->field_count; ++texture_field_index)
                                                                                    {
                                                                                        const won::FieldDesc& candidate = texture_map_type->fields[texture_field_index];
                                                                                        if (candidate.struct_size >= sizeof(won::FieldDesc) && candidate.field_id == texture_field_id && (candidate.flags & won::FieldFlagSerializable) != 0)
                                                                                        {
                                                                                            texture_field = &candidate;
                                                                                            break;
                                                                                        }
                                                                                    }
                                                                                    if (!texture_field || texture_field->offset > texture_map_type->size || texture_field->size > texture_map_type->size - texture_field->offset)
                                                                                    {
                                                                                        continue;
                                                                                    }

                                                                                    void* texture_field_value = static_cast<uint8*>(static_cast<void*>(&texture_map)) + texture_field->offset;
                                                                                    if (!archive.BeginField(texture_field_key.c_str()))
                                                                                    {
                                                                                        continue;
                                                                                    }

                                                                                    if (texture_field->name && std::strcmp(texture_field->name, "texture_asset_path") == 0)
                                                                                    {
                                                                                        uint32 resource_index = invalid_resource_index;
                                                                                        archive.Value(resource_index);
                                                                                        *static_cast<String*>(texture_field_value) = resource_index < texture_resources.size() ? texture_resources[resource_index] : String();
                                                                                    }
                                                                                    else
                                                                                    {
                                                                                        ReadReflectedData(archive, texture_field->value_type, nullptr, texture_field->size, texture_field->array_desc, texture_field_value);
                                                                                    }
                                                                                    archive.EndField();
                                                                                }
                                                                                archive.EndObject();
                                                                            }
                                                                            archive.EndItem();
                                                                        }
                                                                        archive.EndArray();
                                                                    }
                                                                }
                                                                else
                                                                {
                                                                    ReadReflectedData(archive, slot_field->value_type, nullptr, slot_field->size, slot_field->array_desc, slot_field_value);
                                                                }
                                                                archive.EndField();
                                                            }
                                                            archive.EndObject();
                                                        }
                                                        archive.EndItem();
                                                    }
                                                    archive.EndArray();
                                                    archive.EndField();
                                                }
                                                archive.EndObject();
                                                if (!material->slots.empty())
                                                {
                                                    material_comp->SetMaterial(material);
                                                }
                                            }
                                            else
                                            {
                                                EntityRefContext entity_refs = {};
                                                entity_refs.encoding = entity_ref_encoding;
                                                entity_refs.entities = &entities;
                                                ReadReflectedData(archive, field->value_type, nullptr, field->size, field->array_desc, field_value, &entity_refs);
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

            for (ecs::Entity entity : entities)
            {
                if (ecs::TransformComponent* transform = scene.GetComponent<ecs::TransformComponent>(entity))
                {
                    transform->SetDirty();
                }
                if (version < 4)
                {
                    if (ecs::CameraComponent* camera = scene.GetComponent<ecs::CameraComponent>(entity))
                    {
                        camera->SetActive(true);
                    }
                }
                if (ecs::SequenceComponent* sequence = scene.GetComponent<ecs::SequenceComponent>(entity))
                {
                    for (ecs::SequenceTrack& track : sequence->tracks)
                    {
                        std::sort(track.keys.begin(), track.keys.end(), [](const ecs::SequenceKey& lhs, const ecs::SequenceKey& rhs) { return lhs.time < rhs.time; });
                    }
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
                if (ecs::Collider3DComponent* collider = scene.GetComponent<ecs::Collider3DComponent>(entity))
                {
                    collider->SetDirty();
                }
            }
            scene.SetHierarchyTopologyDirty(true);
            scene.SetBVHDirty();
            archive.EndObject();
            return true;
        }

    }

    void LoadScene(JsonArchive& archive, ecs::Scene& scene)
    {
        assert(archive.IsReadMode());
        scene.ClearEntities();
        Vector<ecs::Entity> entities;
        ReadEntities(archive, scene, entities);
    }

    void SaveScene(JsonArchive& archive, const ecs::Scene& scene, const SaveSceneDesc& desc)
    {
        assert(archive.IsWriteMode());
        WriteEntities(archive, scene, desc);
    }

    ecs::Entity LoadSceneAdditive(JsonArchive& archive, ecs::Scene& scene, Vector<ecs::Entity>& out_new_entities, ecs::Entity preallocated_root)
    {
        assert(archive.IsReadMode());
        out_new_entities.clear();
        if (!ReadEntities(archive, scene, out_new_entities, preallocated_root))
        {
            return ecs::INVALID_ENTITY;
        }
        return out_new_entities.empty() ? ecs::INVALID_ENTITY : out_new_entities[0];
    }

    bool SavePrefab(JsonArchive& archive, ecs::Scene& scene, ecs::Entity root)
    {
        assert(archive.IsWriteMode());
        if (root == ecs::INVALID_ENTITY)
        {
            return false;
        }

        const Vector<ecs::Entity> subtree = CollectSubtree(scene, root);
        WriteEntities(archive, scene, SaveSceneDesc{}, &subtree);
        return !archive.HasError();
    }

    bool SaveComponent(JsonArchive& archive, const ecs::Scene& scene, ecs::Entity entity, won::TypeId type_id)
    {
        assert(archive.IsWriteMode());
        if (entity == ecs::INVALID_ENTITY || type_id == 0 || !scene.HasComponent(entity, type_id))
        {
            return false;
        }

        Vector<ecs::Entity> single_entity;
        single_entity.push_back(entity);
        WriteEntities(archive, scene, SaveSceneDesc{}, &single_entity, false, &type_id, EntityRefEncoding::RawId);
        return !archive.HasError();
    }

    bool LoadComponent(JsonArchive& archive, ecs::Scene& scene, ecs::Entity entity, won::TypeId type_id)
    {
        assert(archive.IsReadMode());
        if (entity == ecs::INVALID_ENTITY || type_id == 0 || !scene.IsEntityAlive(entity))
        {
            return false;
        }

        Vector<ecs::Entity> entities;
        return ReadEntities(archive, scene, entities, entity);
    }

    bool SaveEntitySnapshot(JsonArchive& archive, ecs::Scene& scene, ecs::Entity root)
    {
        assert(archive.IsWriteMode());
        if (root == ecs::INVALID_ENTITY)
        {
            return false;
        }

        const Vector<ecs::Entity> subtree = CollectSubtree(scene, root);
        WriteEntities(archive, scene, SaveSceneDesc{}, &subtree, true, nullptr, EntityRefEncoding::RawId);
        return !archive.HasError();
    }

    ecs::Entity LoadEntitySnapshot(JsonArchive& archive, ecs::Scene& scene, Vector<ecs::Entity>& out_entities)
    {
        assert(archive.IsReadMode());
        out_entities.clear();
        if (!ReadEntities(archive, scene, out_entities, ecs::INVALID_ENTITY, true))
        {
            return ecs::INVALID_ENTITY;
        }
        return out_entities.empty() ? ecs::INVALID_ENTITY : out_entities[0];
    }
}
