#include "JsonArchive.h"
#include "FileSystem.h"

#include <nlohmann/json.hpp>

namespace won::serialize
{
    namespace
    {
        enum class JsonContextType
        {
            Value,
            Object,
            Array,
        };

        template <typename T>
        bool ReadIntegerValue(const nlohmann::json& json, T& value)
        {
            if (!json.is_number_integer() && !json.is_number_unsigned())
            {
                return false;
            }
            value = json.get<T>();
            return true;
        }

        template <typename T>
        bool ReadFloatValue(const nlohmann::json& json, T& value)
        {
            if (!json.is_number())
            {
                return false;
            }
            value = json.get<T>();
            return true;
        }
    }

    struct JsonArchive::Impl
    {
        struct Context
        {
            nlohmann::json* node = nullptr;
            JsonContextType type = JsonContextType::Value;
            Size next_index = 0;
        };

        Impl(ArchiveMode mode_in, const JsonArchiveDesc& desc_in)
            : mode(mode_in)
            , desc(desc_in)
        {
        }

        nlohmann::json* CurrentNode()
        {
            return stack.empty() ? &root : stack.back().node;
        }

        const nlohmann::json* CurrentNode() const
        {
            return stack.empty() ? &root : stack.back().node;
        }

        void SetError(const String& error_in)
        {
            if (error.empty())
            {
                error = error_in;
            }
        }

        bool IsReadMode() const
        {
            return mode == ArchiveMode::Read;
        }

        bool IsWriteMode() const
        {
            return mode == ArchiveMode::Write;
        }

        nlohmann::json root;
        Vector<Context> stack;
        ArchiveMode mode = ArchiveMode::Write;
        JsonArchiveDesc desc = {};
        String error;
    };

    JsonArchive::JsonArchive(ArchiveMode mode, const JsonArchiveDesc& desc)
        : impl(std::make_unique<Impl>(mode, desc))
    {
        if (IsWriteMode())
        {
            impl->root = nlohmann::json::object();
        }
    }

    JsonArchive::~JsonArchive() = default;

    bool JsonArchive::IsReadMode() const
    {
        return impl->IsReadMode();
    }

    bool JsonArchive::IsWriteMode() const
    {
        return impl->IsWriteMode();
    }

    bool JsonArchive::LoadFromFile(const String& path)
    {
        if (!IsReadMode())
        {
            impl->SetError("JsonArchive is not in read mode.");
            return false;
        }

        io::FileData file_data = {};
        if (!io::ReadAllBytes(path, &file_data))
        {
            impl->SetError("Failed to read json file: " + path);
            return false;
        }

        try
        {
            impl->root = nlohmann::json::parse(file_data.bytes.begin(), file_data.bytes.end());
        }
        catch (const nlohmann::json::exception&)
        {
            impl->SetError("Failed to parse json file: " + path);
            return false;
        }

        impl->stack.clear();
        return true;
    }

    bool JsonArchive::SaveToFile(const String& path) const
    {
        if (!IsWriteMode())
        {
            impl->SetError("JsonArchive is not in write mode.");
            return false;
        }

        const String content = impl->desc.pretty ? impl->root.dump(4) : impl->root.dump();
        return io::WriteAllBytes(path, reinterpret_cast<const uint8*>(content.data()), content.size());
    }

    bool JsonArchive::HasError() const
    {
        return !impl->error.empty();
    }

    const String& JsonArchive::GetError() const
    {
        return impl->error;
    }

    bool JsonArchive::BeginObject(const char* name)
    {
        nlohmann::json* node = nullptr;
        if (name)
        {
            nlohmann::json* current = impl->CurrentNode();
            if (!current || !current->is_object())
            {
                impl->SetError("JsonArchive object field requires an object context.");
                return false;
            }

            if (IsWriteMode())
            {
                (*current)[name] = nlohmann::json::object();
                node = &(*current)[name];
            }
            else
            {
                auto it = current->find(name);
                if (it == current->end())
                {
                    return false;
                }
                node = &(*it);
            }
        }
        else
        {
            node = impl->CurrentNode();
            if (IsWriteMode())
            {
                *node = nlohmann::json::object();
            }
        }

        if (!node || !node->is_object())
        {
            impl->SetError("JsonArchive expected an object.");
            return false;
        }

        impl->stack.push_back({ node, JsonContextType::Object, 0 });
        return true;
    }

    void JsonArchive::EndObject()
    {
        if (impl->stack.empty() || impl->stack.back().type != JsonContextType::Object)
        {
            impl->SetError("JsonArchive object scope mismatch.");
            return;
        }
        impl->stack.pop_back();
    }

    bool JsonArchive::BeginArray(const char* name)
    {
        nlohmann::json* node = nullptr;
        if (name)
        {
            nlohmann::json* current = impl->CurrentNode();
            if (!current || !current->is_object())
            {
                impl->SetError("JsonArchive array field requires an object context.");
                return false;
            }

            if (IsWriteMode())
            {
                (*current)[name] = nlohmann::json::array();
                node = &(*current)[name];
            }
            else
            {
                auto it = current->find(name);
                if (it == current->end())
                {
                    return false;
                }
                node = &(*it);
            }
        }
        else
        {
            node = impl->CurrentNode();
            if (IsWriteMode())
            {
                *node = nlohmann::json::array();
            }
        }

        if (!node || !node->is_array())
        {
            impl->SetError("JsonArchive expected an array.");
            return false;
        }

        impl->stack.push_back({ node, JsonContextType::Array, 0 });
        return true;
    }

    void JsonArchive::EndArray()
    {
        if (impl->stack.empty() || impl->stack.back().type != JsonContextType::Array)
        {
            impl->SetError("JsonArchive array scope mismatch.");
            return;
        }
        impl->stack.pop_back();
    }

    Size JsonArchive::GetArraySize() const
    {
        const nlohmann::json* node = impl->CurrentNode();
        return node && node->is_array() ? node->size() : 0;
    }

    Vector<String> JsonArchive::GetObjectKeys() const
    {
        Vector<String> keys;
        const nlohmann::json* node = impl->CurrentNode();
        if (!node || !node->is_object())
        {
            return keys;
        }

        keys.reserve(node->size());
        for (auto it = node->begin(); it != node->end(); ++it)
        {
            keys.push_back(it.key());
        }
        return keys;
    }

    bool JsonArchive::HasField(const char* name) const
    {
        const nlohmann::json* node = impl->CurrentNode();
        return node && node->is_object() && name && node->contains(name);
    }

    bool JsonArchive::FieldToString(const char* name, String& out_value) const
    {
        if (!name)
        {
            impl->SetError("JsonArchive field name is null.");
            return false;
        }

        const nlohmann::json* current = impl->CurrentNode();
        if (!current || !current->is_object())
        {
            impl->SetError("JsonArchive field requires an object context.");
            return false;
        }

        auto it = current->find(name);
        if (it == current->end())
        {
            return false;
        }

        const nlohmann::json& value = *it;
        if (value.is_string())
        {
            out_value = value.get<String>();
            return true;
        }
        if (value.is_boolean())
        {
            out_value = value.get<bool>() ? "true" : "false";
            return true;
        }
        if (value.is_number())
        {
            out_value = value.dump();
            return true;
        }

        return false;
    }

    bool JsonArchive::BeginField(const char* name)
    {
        if (!name)
        {
            impl->SetError("JsonArchive field name is null.");
            return false;
        }

        nlohmann::json* current = impl->CurrentNode();
        if (!current || !current->is_object())
        {
            impl->SetError("JsonArchive field requires an object context.");
            return false;
        }

        nlohmann::json* node = nullptr;
        if (IsWriteMode())
        {
            (*current)[name] = nullptr;
            node = &(*current)[name];
        }
        else
        {
            auto it = current->find(name);
            if (it == current->end())
            {
                return false;
            }
            node = &(*it);
        }

        impl->stack.push_back({ node, JsonContextType::Value, 0 });
        return true;
    }

    void JsonArchive::EndField()
    {
        if (impl->stack.empty() || impl->stack.back().type != JsonContextType::Value)
        {
            impl->SetError("JsonArchive field scope mismatch.");
            return;
        }
        impl->stack.pop_back();
    }

    bool JsonArchive::BeginItem()
    {
        if (impl->stack.empty() || impl->stack.back().type != JsonContextType::Array)
        {
            impl->SetError("JsonArchive item requires an array context.");
            return false;
        }

        Impl::Context& context = impl->stack.back();
        nlohmann::json* node = nullptr;
        if (IsWriteMode())
        {
            context.node->push_back(nullptr);
            node = &context.node->back();
        }
        else
        {
            if (context.next_index >= context.node->size())
            {
                return false;
            }
            node = &(*context.node)[context.next_index];
        }
        ++context.next_index;

        impl->stack.push_back({ node, JsonContextType::Value, 0 });
        return true;
    }

    void JsonArchive::EndItem()
    {
        if (impl->stack.empty() || impl->stack.back().type != JsonContextType::Value)
        {
            impl->SetError("JsonArchive item scope mismatch.");
            return;
        }
        impl->stack.pop_back();
    }

    bool JsonArchive::Value(bool& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        if (!node || !node->is_boolean())
        {
            impl->SetError("JsonArchive expected bool value.");
            return false;
        }
        value = node->get<bool>();
        return true;
    }

    bool JsonArchive::Value(int8& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(uint8& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(int16& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(uint16& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(int32& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(uint32& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(int64& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(uint64& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadIntegerValue(*node, value);
    }

    bool JsonArchive::Value(float& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadFloatValue(*node, value);
    }

    bool JsonArchive::Value(double& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        return node && ReadFloatValue(*node, value);
    }

    bool JsonArchive::Value(String& value)
    {
        nlohmann::json* node = impl->CurrentNode();
        if (IsWriteMode())
        {
            *node = value;
            return true;
        }
        if (!node || !node->is_string())
        {
            impl->SetError("JsonArchive expected string value.");
            return false;
        }
        value = node->get<String>();
        return true;
    }

    void Serialize(JsonArchive& archive, bool& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, int8& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, uint8& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, int16& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, uint16& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, int32& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, uint32& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, int64& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, uint64& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, float& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, double& value) { archive.Value(value); }
    void Serialize(JsonArchive& archive, String& value) { archive.Value(value); }

    void Serialize(JsonArchive& archive, float2& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, float3& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.Item(value.z);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, float4& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.Item(value.z);
            archive.Item(value.w);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, int2& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, int3& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.Item(value.z);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, int4& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.Item(value.z);
            archive.Item(value.w);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, uint2& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, uint3& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.Item(value.z);
            archive.EndArray();
        }
    }

    void Serialize(JsonArchive& archive, uint4& value)
    {
        if (archive.BeginArray())
        {
            archive.Item(value.x);
            archive.Item(value.y);
            archive.Item(value.z);
            archive.Item(value.w);
            archive.EndArray();
        }
    }
}
