#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <string>

namespace won::utils
{
    WONENGINE_API WString DecodeUtf8(StringView input);
    WONENGINE_API String EncodeUtf8(WStringView input);
    WONENGINE_API String ToUpper(StringView input);
    WONENGINE_API String ToLower(StringView input);
    WONENGINE_API bool StartsWith(StringView input, StringView prefix);
    WONENGINE_API uint64 Hash(StringView input);
    WONENGINE_API String GetCurrentDateTime(StringView format = "%Y-%m-%d_%H-%M-%S");
}
