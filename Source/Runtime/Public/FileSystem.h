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

    WONENGINE_API bool Exists(const String& path);
    WONENGINE_API bool CreateDirectories(const String& path);
    WONENGINE_API bool ReadAllBytes(const String& path, FileData* out_data);
    WONENGINE_API bool WriteAllBytes(const String& path, const uint8* data, Size size);

    WONENGINE_API String GetWorkingDirectory();
    WONENGINE_API String GetExecutableDirectory();
    WONENGINE_API bool IsDirectory(const String& path);
    WONENGINE_API bool IsFile(const String& path);
    WONENGINE_API String GetExtension(const String& path);
    WONENGINE_API String GetFilename(const String& path);
}
