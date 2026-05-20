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

    struct DirectoryEntry
    {
        String path;
        bool is_directory = false;
        bool is_file = false;
    };

    class DirectoryWatcher
    {
    public:
        struct FileChange
        {
            String path;
        };

        virtual ~DirectoryWatcher() = default;
        virtual void Poll(Vector<FileChange>* out_changes) = 0;
    };

    WONENGINE_API bool Exists(const String& path);
    WONENGINE_API bool CreateDirectories(const String& path);
    WONENGINE_API bool ReadAllBytes(const String& path, FileData* out_data);
    WONENGINE_API bool WriteAllBytes(const String& path, const uint8* data, Size size);
    WONENGINE_API bool GetLastTimestamp(const String& path, uint64* out_timestamp);
    WONENGINE_API bool EnumerateDirectoryRecursive(const String& root_path, Vector<DirectoryEntry>* out_entries);
    WONENGINE_API std::unique_ptr<DirectoryWatcher> CreateDirectoryWatcher(const String& directory_path, bool recursive);

    WONENGINE_API String GetWorkingDirectory();
    WONENGINE_API String GetExecutableDirectory();
    WONENGINE_API bool IsDirectory(const String& path);
    WONENGINE_API bool IsFile(const String& path);
    WONENGINE_API String GetExtension(const String& path);
    WONENGINE_API String ReplaceExtension(const String& path, const String& ext);
    WONENGINE_API String GetFilename(const String& path);
    WONENGINE_API String GetDirectoryFromPath(const String& path);
    WONENGINE_API bool IsAbsolutePath(const String& path);
    WONENGINE_API String CombinePath(const String& lhs, const String& rhs);
    WONENGINE_API String NormalizePath(const String& path);
    WONENGINE_API String GetAbsolutePath(const String& path);
    WONENGINE_API String GetRelativePath(const String& root_path, const String& path);
    WONENGINE_API bool CreateFolder(const String& path);
}
