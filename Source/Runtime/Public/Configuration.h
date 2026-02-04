#pragma once
#include "Types.h"

namespace won::config
{
    void SetString(const std::string& key, const std::string& value);
    bool TryGetString(const std::string& key, std::string& out_value);
    void SetInt(const std::string& key, int value);
    bool TryGetInt(const std::string& key, int& out_value);

    void Clear();
}
