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

    struct FileDialogDesc
    {
        void* owner_window = nullptr;
        String title;
        String initial_directory;
        String default_file_name;
        String default_extension;
        String filter_name;
        String filter_pattern;
    };

    WONENGINE_API bool Exists(const String& path);
    WONENGINE_API bool CreateDirectories(const String& path);
    WONENGINE_API bool CopyFileTo(const String& from, const String& to, bool overwrite);
    WONENGINE_API bool RemoveDirectoryRecursive(const String& path);
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
    WONENGINE_API bool OpenFileDialog(String& out_path, const FileDialogDesc& desc);
    WONENGINE_API bool SaveFileDialog(String& out_path, const FileDialogDesc& desc);

    // Reveal a file or folder in the OS file manager (selecting it when possible).
    WONENGINE_API bool ShowInFileManager(const String& path);
    // Launch an executable with arguments in a working directory (detached).
    WONENGINE_API bool LaunchProcess(const String& executable, const String& arguments, const String& working_directory);

    // %APPDATA%\<app_name>\ - save data and settings (AppData/Roaming in Windows)
    WONENGINE_API String GetSaveDirectory(const String& app_name);
    // %LOCALAPPDATA%\<app_name>\ - caches and other recreatable data (AppData/Local in Windows)
    WONENGINE_API String GetCacheDirectory(const String& app_name);
}
