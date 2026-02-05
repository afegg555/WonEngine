#pragma once
#include "Types.h"

namespace won::config
{
    void SetString(const String& key, const String& value);
    bool TryGetString(const String& key, String& out_value);
    void SetInt(const String& key, int value);
    bool TryGetInt(const String& key, int& out_value);

    void Clear();
}
