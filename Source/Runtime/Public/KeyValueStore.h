#pragma once
#include "Types.h"

#include <cerrno>
#include <cstdlib>

namespace won::config
{
    class KeyValueStore
    {
    public:
        void SetString(const char* key, const char* value)
        {
            if (!key) return;
            values[key] = value ? value : "";
        }

        const char* GetString(const char* key) const
        {
            if (!key) return nullptr;
            auto it = values.find(key);
            return it != values.end() ? it->second.c_str() : nullptr;
        }

        void SetInt(const char* key, int value)
        {
            if (!key) return;
            values[key] = std::to_string(value);
        }

        bool GetInt(const char* key, int& out_value) const
        {
            if (!key) return false;
            auto it = values.find(key);
            if (it == values.end()) return false;
            char* end = nullptr;
            errno = 0;
            const long parsed = std::strtol(it->second.c_str(), &end, 10);
            if (end == it->second.c_str() || *end != '\0' || errno == ERANGE) return false;
            out_value = static_cast<int>(parsed);
            return true;
        }

        void SetFloat(const char* key, float value)
        {
            if (!key) return;
            values[key] = std::to_string(value);
        }

        bool GetFloat(const char* key, float& out_value) const
        {
            if (!key) return false;
            auto it = values.find(key);
            if (it == values.end()) return false;
            char* end = nullptr;
            errno = 0;
            const float parsed = std::strtof(it->second.c_str(), &end);
            if (end == it->second.c_str() || *end != '\0' || errno == ERANGE) return false;
            out_value = parsed;
            return true;
        }

        void SetBool(const char* key, bool value)
        {
            if (!key) return;
            values[key] = value ? "true" : "false";
        }

        bool GetBool(const char* key, bool& out_value) const
        {
            if (!key) return false;
            auto it = values.find(key);
            if (it == values.end()) return false;
            if (it->second == "true"  || it->second == "1") { out_value = true;  return true; }
            if (it->second == "false" || it->second == "0") { out_value = false; return true; }
            return false;
        }

        bool HasKey(const char* key) const
        {
            if (!key) return false;
            return values.find(key) != values.end();
        }

        void RemoveKey(const char* key)
        {
            if (!key) return;
            values.erase(key);
        }

        void Clear()
        {
            values.clear();
        }

        uint32 GetKeyCount() const
        {
            return static_cast<uint32>(values.size());
        }

        const char* GetKey(uint32 index) const
        {
            if (index >= values.size()) return nullptr;
            auto it = values.begin();
            std::advance(it, index);
            return it->first.c_str();
        }

    private:
        Map<String, String> values;
    };
}
