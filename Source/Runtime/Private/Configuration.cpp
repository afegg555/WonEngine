#include "Configuration.h"
#include "FileSystem.h"

#include <sstream>

namespace won::config
{
    static UnorderedMap<String, String> values;

    void SetString(const String& key, const String& value)
    {
        values[key] = value;
    }

    bool TryGetString(const String& key, String& out_value)
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        out_value = it->second;
        return true;
    }

    void SetInt(const String& key, int value)
    {
        values[key] = std::to_string(value);
    }

    bool TryGetInt(const String& key, int& out_value)
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        out_value = std::stoi(it->second);
        return true;
    }

    void SetFloat(const String& key, float value)
    {
        values[key] = std::to_string(value);
    }

    bool TryGetFloat(const String& key, float& out_value)
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        out_value = std::stof(it->second);
        return true;
    }

    void SetBool(const String& key, bool value)
    {
        values[key] = value ? "true" : "false";
    }

    bool TryGetBool(const String& key, bool& out_value)
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        out_value = it->second == "true" || it->second == "1";
        return true;
    }

    bool LoadFromFile(const String& path)
    {
        io::FileData file_data = {};
        if (!io::ReadAllBytes(path, &file_data))
        {
            return false;
        }

        String content(reinterpret_cast<const char*>(file_data.bytes.data()), file_data.bytes.size());
        std::stringstream stream(content);
        String line;
        while (std::getline(stream, line))
        {
            if (line.empty() || line[0] == '#')
            {
                continue;
            }

            const Size separator_pos = line.find('=');
            if (separator_pos == String::npos)
            {
                continue;
            }

            String key = line.substr(0, separator_pos);
            String value = line.substr(separator_pos + 1);
            if (!key.empty())
            {
                values[key] = value;
            }
        }

        return true;
    }

    bool SaveToFile(const String& path)
    {
        String content;
        for (const auto& entry : values)
        {
            content += entry.first;
            content += "=";
            content += entry.second;
            content += "\n";
        }

        return io::WriteAllBytes(path, reinterpret_cast<const uint8*>(content.data()), content.size());
    }

    void Clear()
    {
        values.clear();
    }
}
