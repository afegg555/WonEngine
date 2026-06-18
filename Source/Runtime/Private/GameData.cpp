#include "GameData.h"
#include "FileSystem.h"
#include "JsonArchive.h"

#include <algorithm>

namespace won::game
{
    const GameData::Field* GameData::FindField(const char* key, const char* type) const
    {
        if (!key || !type) return nullptr;
        for (const Field& f : fields)
        {
            if (f.key == key && f.type == type)
                return &f;
        }
        return nullptr;
    }

    bool GameData::SetString(const char* key, const char* value)
    {
        if (!FindField(key, "string")) return false;
        store.SetString(key, value);
        return true;
    }

    const char* GameData::GetString(const char* key) const
    {
        if (!FindField(key, "string")) return nullptr;
        return store.GetString(key);
    }

    bool GameData::SetInt(const char* key, int value)
    {
        if (!FindField(key, "int")) return false;
        store.SetInt(key, value);
        return true;
    }

    bool GameData::GetInt(const char* key, int& out_value) const
    {
        if (!FindField(key, "int")) return false;
        return store.GetInt(key, out_value);
    }

    bool GameData::SetFloat(const char* key, float value)
    {
        if (!FindField(key, "float")) return false;
        store.SetFloat(key, value);
        return true;
    }

    bool GameData::GetFloat(const char* key, float& out_value) const
    {
        if (!FindField(key, "float")) return false;
        return store.GetFloat(key, out_value);
    }

    bool GameData::SetBool(const char* key, bool value)
    {
        if (!FindField(key, "bool")) return false;
        store.SetBool(key, value);
        return true;
    }

    bool GameData::GetBool(const char* key, bool& out_value) const
    {
        if (!FindField(key, "bool")) return false;
        return store.GetBool(key, out_value);
    }

    void GameData::RemoveKey(const char* key)
    {
        store.RemoveKey(key);
    }

    uint32 GameData::GetKeyCount() const
    {
        return store.GetKeyCount();
    }

    const char* GameData::GetKey(uint32 index) const
    {
        return store.GetKey(index);
    }

    void GameData::AddField(const char* key, const char* type, const char* default_value)
    {
        if (!key || !type) return;
        for (Field& f : fields)
        {
            if (f.key == key)
            {
                f.type = type;
                f.default_value = default_value ? default_value : "";
                return;
            }
        }
        fields.push_back({ key, type, default_value ? default_value : "" });
    }

    void GameData::RemoveField(const char* key)
    {
        if (!key) return;
        fields.erase(
            std::remove_if(fields.begin(), fields.end(),
                [key](const Field& f) { return f.key == key; }),
            fields.end());
        store.RemoveKey(key);
    }

    uint32 GameData::GetFieldCount() const
    {
        return static_cast<uint32>(fields.size());
    }

    const char* GameData::GetFieldKey(uint32 index) const
    {
        if (index >= fields.size()) return nullptr;
        return fields[index].key.c_str();
    }

    const char* GameData::GetFieldType(uint32 index) const
    {
        if (index >= fields.size()) return nullptr;
        return fields[index].type.c_str();
    }

    const char* GameData::GetFieldDefault(uint32 index) const
    {
        if (index >= fields.size()) return nullptr;
        return fields[index].default_value.c_str();
    }

    bool GameData::LoadSchema(const char* path)
    {
        fields.clear();
        if (!path) return false;
        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(path) || !archive.BeginObject()) return false;
        if (!archive.BeginArray("fields")) { archive.EndObject(); return false; }
        const Size count = archive.GetArraySize();
        for (Size i = 0; i < count; ++i)
        {
            if (!archive.BeginItem() || !archive.BeginObject()) break;
            Field field;
            archive.FieldToString("key", field.key);
            archive.FieldToString("type", field.type);
            archive.FieldToString("default", field.default_value);
            archive.EndObject();
            archive.EndItem();
            if (!field.key.empty() && !field.type.empty())
                fields.push_back(std::move(field));
        }
        archive.EndArray();
        archive.EndObject();
        return !archive.HasError();
    }

    bool GameData::SaveSchema(const char* path) const
    {
        if (!path) return false;
        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        archive.BeginObject();
        archive.BeginArray("fields");
        for (const Field& f : fields)
        {
            archive.BeginItem();
            archive.BeginObject();
            String key = f.key;
            String type = f.type;
            String default_val = f.default_value;
            archive.Field("key", key);
            archive.Field("type", type);
            archive.Field("default", default_val);
            archive.EndObject();
            archive.EndItem();
        }
        archive.EndArray();
        archive.EndObject();
        return !archive.HasError() && archive.SaveToFile(path);
    }

    bool GameData::Load(const char* app_name, const char* filename)
    {
        if (!app_name || !filename) return false;
        const String path = io::CombinePath(io::GetSaveDirectory(app_name), filename);
        if (io::Exists(path))
        {
            store.Clear();
            serialize::JsonArchive archive(serialize::ArchiveMode::Read);
            if (archive.LoadFromFile(path) && archive.BeginObject())
            {
                for (const String& key : archive.GetObjectKeys())
                {
                    String value;
                    if (archive.FieldToString(key.c_str(), value))
                        store.SetString(key.c_str(), value.c_str());
                }
                archive.EndObject();
            }
        }
        for (const Field& f : fields)
        {
            if (!store.GetString(f.key.c_str()))
                store.SetString(f.key.c_str(), f.default_value.c_str());
        }
        return true;
    }

    bool GameData::Save(const char* app_name, const char* filename)
    {
        if (!app_name || !filename) return false;
        const String dir = io::GetSaveDirectory(app_name);
        if (!io::CreateDirectories(dir)) return false;
        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        archive.BeginObject();
        for (const Field& f : fields)
        {
            const char* raw = store.GetString(f.key.c_str());
            String value = raw ? raw : f.default_value;
            archive.Field(f.key.c_str(), value);
        }
        archive.EndObject();
        return !archive.HasError() && archive.SaveToFile(io::CombinePath(dir, filename).c_str());
    }
}
