#pragma once
#include "KeyValueStore.h"
#include "RuntimeExport.h"

namespace won::game
{
    class WONENGINE_API GameData
    {
    public:
        bool SetString(const char* key, const char* value);
        const char* GetString(const char* key) const;
        bool SetInt(const char* key, int value);
        bool GetInt(const char* key, int& out_value) const;
        bool SetFloat(const char* key, float value);
        bool GetFloat(const char* key, float& out_value) const;
        bool SetBool(const char* key, bool value);
        bool GetBool(const char* key, bool& out_value) const;
        void RemoveKey(const char* key);
        uint32 GetKeyCount() const;
        const char* GetKey(uint32 index) const;

        void AddField(const char* key, const char* type, const char* default_value);
        void RemoveField(const char* key);
        uint32 GetFieldCount() const;
        const char* GetFieldKey(uint32 index) const;
        const char* GetFieldType(uint32 index) const;
        const char* GetFieldDefault(uint32 index) const;

        bool LoadSchema(const char* path);
        bool SaveSchema(const char* path) const;

        bool Load(const char* app_name, const char* filename);
        bool Save(const char* app_name, const char* filename);

    private:
        struct Field
        {
            String key;
            String type;
            String default_value;
        };

        const Field* FindField(const char* key, const char* type) const;

        config::KeyValueStore store;
        Vector<Field> fields;
    };
}
