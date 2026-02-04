#include "Configuration.h"

namespace won::config
{
    static std::unordered_map<std::string, std::string> values;

    void SetString(const std::string& key, const std::string& value)
    {
        values[key] = value;
    }

    bool TryGetString(const std::string& key, std::string& out_value)
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        out_value = it->second;
        return true;
    }

    void SetInt(const std::string& key, int value)
    {
        values[key] = std::to_string(value);
    }

    bool TryGetInt(const std::string& key, int& out_value)
    {
        auto it = values.find(key);
        if (it == values.end())
        {
            return false;
        }

        out_value = std::stoi(it->second);
        return true;
    }

    void Clear()
    {
        values.clear();
    }
}