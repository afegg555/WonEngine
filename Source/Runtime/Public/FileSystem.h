#pragma once
#include "RuntimeExport.h"
#include "Types.h"

#include <string>

namespace won::io
{
    struct FileData
    {
        Vector<uint8> bytes;
    };

    WONENGINE_API bool Exists(const std::string& path);
    WONENGINE_API bool CreateDirectories(const std::string& path);
    WONENGINE_API bool ReadAllBytes(const std::string& path, FileData* out_data);
    WONENGINE_API bool WriteAllBytes(const std::string& path, const uint8* data, Size size);

    WONENGINE_API std::string GetWorkingDirectory();
    WONENGINE_API std::string GetExecutableDirectory();
    WONENGINE_API bool IsDirectory(const std::string& path);
    WONENGINE_API bool IsFile(const std::string& path);
    WONENGINE_API std::string GetExtension(const std::string& path);
    WONENGINE_API std::string GetFilename(const std::string& path);
}
