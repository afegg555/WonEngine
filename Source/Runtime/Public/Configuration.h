#pragma once
#include "RuntimeExport.h"
#include "Types.h"

namespace won::config
{
    WONENGINE_API void SetString(const String& key, const String& value);
    WONENGINE_API bool TryGetString(const String& key, String& out_value);
    WONENGINE_API void SetInt(const String& key, int value);
    WONENGINE_API bool TryGetInt(const String& key, int& out_value);
    WONENGINE_API void SetFloat(const String& key, float value);
    WONENGINE_API bool TryGetFloat(const String& key, float& out_value);
    WONENGINE_API void SetBool(const String& key, bool value);
    WONENGINE_API bool TryGetBool(const String& key, bool& out_value);
    WONENGINE_API bool LoadFromFile(const String& path);
    WONENGINE_API bool SaveToFile(const String& path);

    WONENGINE_API void Clear();
}
