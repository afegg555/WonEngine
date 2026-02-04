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

    class WONENGINE_API FileSystem
    {
    public:
        static bool Exists(const std::string& path);
        static bool CreateDirectories(const std::string& path);
        static bool ReadAllBytes(const std::string& path, FileData* out_data);
        static bool WriteAllBytes(const std::string& path, const uint8* data, Size size);

        static std::string GetWorkingDirectory();
        static std::string GetExecutableDirectory();
        static bool IsDirectory(const std::string& path);
        static bool IsFile(const std::string& path);
        static std::string GetExtension(const std::string& path);
        static std::string GetFilename(const std::string& path);
    };
}
