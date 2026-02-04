#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <string>

namespace won::utils
{
    WONENGINE_API std::wstring ToWideString(const std::string& str);
    WONENGINE_API std::string ToString(const std::wstring& wstr);
    WONENGINE_API std::string ToUpper(StringView input);
    WONENGINE_API std::string ToLower(StringView input);
    WONENGINE_API uint64 Hash(StringView input);
}
