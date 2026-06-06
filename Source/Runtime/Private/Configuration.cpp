#include "Configuration.h"
#include "JsonArchive.h"

#include <cerrno>
#include <cstdlib>

namespace won::config
{
    void Configuration::SetString(const char* key, const char* value)
    {
        if (!key)
        {
            return;
        }

        values[key] = value ? value : "";
    }

    const char* Configuration::GetString(const char* key) const
    {
        if (!key)
        {
            return nullptr;
        }

        auto it = values.find(key);
        if (it == values.end())
        {
            return nullptr;
        }

        return it->second.c_str();
    }

    void Configuration::SetInt(const char* key, int value)
    {
        if (!key)
        {
            return;
        }

        values[key] = std::to_string(value);
    }

    bool Configuration::GetInt(const char* key, int& out_value) const
    {
        if (!key)
        {
            return false;
        }

        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const long parsed = std::strtol(it->second.c_str(), &end, 10);
        if (end == it->second.c_str() || *end != '\0' || errno == ERANGE)
        {
            return false;
        }

        out_value = static_cast<int>(parsed);
        return true;
    }

    void Configuration::SetFloat(const char* key, float value)
    {
        if (!key)
        {
            return;
        }

        values[key] = std::to_string(value);
    }

    bool Configuration::GetFloat(const char* key, float& out_value) const
    {
        if (!key)
        {
            return false;
        }

        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        char* end = nullptr;
        errno = 0;
        const float parsed = std::strtof(it->second.c_str(), &end);
        if (end == it->second.c_str() || *end != '\0' || errno == ERANGE)
        {
            return false;
        }

        out_value = parsed;
        return true;
    }

    void Configuration::SetBool(const char* key, bool value)
    {
        if (!key)
        {
            return;
        }

        values[key] = value ? "true" : "false";
    }

    bool Configuration::GetBool(const char* key, bool& out_value) const
    {
        if (!key)
        {
            return false;
        }

        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        if (it->second == "true" || it->second == "1")
        {
            out_value = true;
            return true;
        }
        if (it->second == "false" || it->second == "0")
        {
            out_value = false;
            return true;
        }

        return false;
    }

    bool Configuration::LoadFromCommandLine(int argc, char** argv)
    {
        values.clear();
        if (argc < 1 || argv == nullptr)
        {
            return false;
        }

        for (int arg_index = 1; arg_index < argc; ++arg_index)
        {
            if (argv[arg_index] == nullptr)
            {
                return false;
            }

            values[std::to_string(arg_index - 1)] = argv[arg_index];
        }

        return true;
    }

    bool Configuration::LoadFromFile(const char* path)
    {
        values.clear();
        if (!path)
        {
            return false;
        }

        serialize::JsonArchive archive(serialize::ArchiveMode::Read);
        if (!archive.LoadFromFile(path) || !archive.BeginObject())
        {
            return false;
        }

        Vector<String> keys = archive.GetObjectKeys();
        for (const String& key : keys)
        {
            String value;
            if (archive.FieldToString(key.c_str(), value))
            {
                values[key] = value;
            }
        }

        archive.EndObject();
        return !archive.HasError();
    }

    bool Configuration::SaveToFile(const char* path) const
    {
        if (!path)
        {
            return false;
        }

        serialize::JsonArchive archive(serialize::ArchiveMode::Write);
        archive.BeginObject();
        for (const auto& entry : values)
        {
            String value = entry.second;
            archive.Field(entry.first.c_str(), value);
        }
        archive.EndObject();
        return !archive.HasError() && archive.SaveToFile(path);
    }
}
