#include "Configuration.h"

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

    void Clear()
    {
        values.clear();
    }
}