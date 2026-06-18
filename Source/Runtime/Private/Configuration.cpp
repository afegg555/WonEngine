#include "Configuration.h"
#include "JsonArchive.h"

namespace won::config
{
    void Configuration::SetString(const char* key, const char* value)
    {
        store.SetString(key, value);
    }

    const char* Configuration::GetString(const char* key) const
    {
        return store.GetString(key);
    }

    void Configuration::SetInt(const char* key, int value)
    {
        store.SetInt(key, value);
    }

    bool Configuration::GetInt(const char* key, int& out_value) const
    {
        return store.GetInt(key, out_value);
    }

    void Configuration::SetFloat(const char* key, float value)
    {
        store.SetFloat(key, value);
    }

    bool Configuration::GetFloat(const char* key, float& out_value) const
    {
        return store.GetFloat(key, out_value);
    }

    void Configuration::SetBool(const char* key, bool value)
    {
        store.SetBool(key, value);
    }

    bool Configuration::GetBool(const char* key, bool& out_value) const
    {
        return store.GetBool(key, out_value);
    }

    void Configuration::RemoveKey(const char* key)
    {
        store.RemoveKey(key);
    }

    uint32 Configuration::GetKeyCount() const
    {
        return store.GetKeyCount();
    }

    const char* Configuration::GetKey(uint32 index) const
    {
        return store.GetKey(index);
    }

    bool Configuration::LoadFromFile(const char* path)
    {
        store.Clear();
        if (!path) return false;
        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(path) || !archive.BeginObject()) return false;
        for (const String& key : archive.GetObjectKeys())
        {
            String value;
            if (archive.FieldToString(key.c_str(), value))
                store.SetString(key.c_str(), value.c_str());
        }
        archive.EndObject();
        return !archive.HasError();
    }

    bool Configuration::SaveToFile(const char* path) const
    {
        if (!path) return false;
        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        archive.BeginObject();
        for (uint32 i = 0; i < store.GetKeyCount(); ++i)
        {
            const char* key = store.GetKey(i);
            if (!key) continue;
            String value = store.GetString(key) ? store.GetString(key) : "";
            archive.Field(key, value);
        }
        archive.EndObject();
        return !archive.HasError() && archive.SaveToFile(path);
    }

    bool Configuration::LoadFromCommandLine(int argc, char** argv)
    {
        store.Clear();
        if (argc < 1 || argv == nullptr) return false;
        for (int i = 1; i < argc; ++i)
        {
            if (argv[i] == nullptr) return false;
            store.SetString(std::to_string(i - 1).c_str(), argv[i]);
        }
        return true;
    }
}
