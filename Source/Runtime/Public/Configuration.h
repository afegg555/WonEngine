#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::config
{
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
        bool LoadFromCommandLine(int argc, char** argv);
        bool LoadFromFile(const char* path);
        bool SaveToFile(const char* path) const;

    private:
        Map<String, String> values;
    };
}
