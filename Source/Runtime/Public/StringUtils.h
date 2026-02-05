#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <string>

namespace won::utils
{
    WONENGINE_API WString ToWideString(const String& str);
    WONENGINE_API String ToString(const WString& wstr);
    WONENGINE_API String ToUpper(StringView input);
    WONENGINE_API String ToLower(StringView input);
    WONENGINE_API uint64 Hash(StringView input);
}
