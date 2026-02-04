#pragma once

#include "RuntimeExport.h"
#include "Types.h"

#include <string>

namespace won::utils
{
    WONENGINE_API bool IsDirectory(const std::string& path);
    WONENGINE_API bool IsFile(const std::string& path);
    WONENGINE_API std::string GetExtension(const std::string& path);
    WONENGINE_API std::string GetFilename(const std::string& path);
}
