#pragma once
#include "KeyValueStore.h"
#include "RuntimeExport.h"

namespace won::config
{
    // Engine/app settings as free-form string key-value pairs, sourced from config files and the command line.
    class WONENGINE_API Configuration
    {
    public:
        void SetString(const char* key, const char* value);
        const char* GetString(const char* key) const;
        void SetInt(const char* key, int value);
        bool GetInt(const char* key, int& out_value) const;
        void SetFloat(const char* key, float value);
        bool GetFloat(const char* key, float& out_value) const;
        void SetBool(const char* key, bool value);
        bool GetBool(const char* key, bool& out_value) const;
        bool HasKey(const char* key) const;
        void RemoveKey(const char* key);
        uint32 GetKeyCount() const;
        const char* GetKey(uint32 index) const;

        bool LoadFromFile(const char* path);
        bool SaveToFile(const char* path) const;
        bool LoadFromCommandLine(int argc, char** argv);

    private:
        KeyValueStore store;
    };
}
