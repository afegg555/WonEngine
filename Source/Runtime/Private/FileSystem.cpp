#include "FileSystem.h"
#include "Platform.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace won::io
{
    namespace
    {
        class DirectoryWatcherInternal : public DirectoryWatcher
        {
        public:
            DirectoryWatcherInternal(const String& directory_path_in, bool recursive_in)
                : recursive(recursive_in)
            {
#if defined(_WIN32)
                std::error_code error;
                std::filesystem::path fs_path = std::filesystem::absolute(std::filesystem::u8path(directory_path_in), error).lexically_normal();
                if (error || !std::filesystem::is_directory(fs_path, error))
                {
                    return;
                }

                directory_path = fs_path.generic_string();
                directory_handle = CreateFileW(fs_path.wstring().c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
                if (directory_handle == INVALID_HANDLE_VALUE)
                {
                    return;
                }

                change_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
                if (!change_event)
                {
                    CloseHandle(directory_handle);
                    directory_handle = INVALID_HANDLE_VALUE;
                    return;
                }

                overlapped.hEvent = change_event;
                change_buffer.resize(64 * 1024);
                BeginRead();
#endif
            }

            ~DirectoryWatcherInternal() override
            {
#if defined(_WIN32)
                if (directory_handle != INVALID_HANDLE_VALUE)
                {
                    CancelIoEx(directory_handle, nullptr);
                }
                if (read_pending && directory_handle != INVALID_HANDLE_VALUE)
                {
                    DWORD ignored_bytes = 0;
                    GetOverlappedResult(directory_handle, &overlapped, &ignored_bytes, TRUE);
                    read_pending = false;
                }
                if (change_event)
                {
                    CloseHandle(change_event);
                    change_event = nullptr;
                }
                if (directory_handle != INVALID_HANDLE_VALUE)
                {
                    CloseHandle(directory_handle);
                    directory_handle = INVALID_HANDLE_VALUE;
                }
#endif
            }

            bool IsValid() const
            {
#if defined(_WIN32)
                return directory_handle != INVALID_HANDLE_VALUE && change_event != nullptr;
#else
                return false;
#endif
            }

            void Poll(Vector<FileChange>* out_changes) override
            {
                if (!out_changes)
                {
                    return;
                }

                out_changes->clear();
#if defined(_WIN32)
                if (!IsValid())
                {
                    return;
                }

                if (!read_pending && !BeginRead())
                {
                    return;
                }

                const DWORD wait_result = WaitForSingleObject(change_event, 0);
                if (wait_result == WAIT_TIMEOUT) // not change yet
                {
                    return;
                }
                if (wait_result != WAIT_OBJECT_0) // error
                {
                    CancelIoEx(directory_handle, &overlapped);
                    read_pending = false;
                    BeginRead();
                    return;
                }

                // changed. wait_result = WAIT_OBJECT_0
                DWORD bytes_returned = 0;
                if (!GetOverlappedResult(directory_handle, &overlapped, &bytes_returned, FALSE) || bytes_returned == 0)
                {
                    read_pending = false;
                    BeginRead();
                    return;
                }

                uint8* current = change_buffer.data();
                while (true)
                {
                    FILE_NOTIFY_INFORMATION* info = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(current);
                    std::wstring file_name(info->FileName, info->FileNameLength / sizeof(wchar_t));
                    std::filesystem::path changed_path = std::filesystem::u8path(directory_path) / std::filesystem::path(file_name);
                    String path = changed_path.lexically_normal().generic_string();
                    bool exists = false;
                    for (const FileChange& change : *out_changes)
                    {
                        if (change.path == path)
                        {
                            exists = true;
                            break;
                        }
                    }
                    if (!exists)
                    {
                        out_changes->push_back({ path });
                    }

                    if (info->NextEntryOffset == 0)
                    {
                        break;
                    }
                    current += info->NextEntryOffset;
                }

                read_pending = false;
                BeginRead();
#endif
            }

        private:
            bool BeginRead()
            {
#if defined(_WIN32)
                if (!IsValid() || change_buffer.empty())
                {
                    return false;
                }

                overlapped = {};
                overlapped.hEvent = change_event;
                ResetEvent(change_event);

                const DWORD notify_filter = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;
                read_pending = ReadDirectoryChangesW(directory_handle, change_buffer.data(), static_cast<DWORD>(change_buffer.size()), recursive ? TRUE : FALSE, notify_filter, nullptr, &overlapped, nullptr) != FALSE;
                return read_pending;
#else
                return false;
#endif
            }

            String directory_path;
            bool recursive = false;
#if defined(_WIN32)
            HANDLE directory_handle = INVALID_HANDLE_VALUE;
            HANDLE change_event = nullptr;
            OVERLAPPED overlapped = {};
#endif
            bool read_pending = false;
            Vector<uint8> change_buffer;
        };
    }

    bool Exists(const String& path)
    {
        std::error_code error;
        return std::filesystem::exists(std::filesystem::u8path(path), error);
    }

    bool CreateDirectories(const String& path)
    {
        if (path.empty())
        {
            return false;
        }

        std::error_code error;
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        if (std::filesystem::exists(fs_path, error))
        {
            return true;
        }

        return std::filesystem::create_directories(fs_path, error);
    }

    bool ReadAllBytes(const String& path, FileData* out_data)
    {
        if (out_data == nullptr)
        {
            return false;
        }

        std::ifstream file(std::filesystem::u8path(path), std::ios::binary | std::ios::ate);
        if (!file)
        {
            return false;
        }

        std::ifstream::pos_type size = file.tellg(); // size in bytes
        if (size < 0)
        {
            return false;
        }

        out_data->bytes.resize(static_cast<Size>(size));
        file.seekg(0, std::ios::beg);
        if (!out_data->bytes.empty())
        {
            file.read(reinterpret_cast<char*>(out_data->bytes.data()), static_cast<std::streamsize>(size));
        }

        return file.good();
    }

    bool WriteAllBytes(const String& path, const uint8* data, Size size)
    {
        if (data == nullptr && size > 0)
        {
            return false;
        }

        std::filesystem::path fs_path = std::filesystem::u8path(path);
        std::error_code error;
        if (fs_path.has_parent_path())
        {
            std::filesystem::create_directories(fs_path.parent_path(), error);
        }

        std::ofstream file(fs_path, std::ios::binary | std::ios::trunc); // clear and overwrites
        if (!file)
        {
            return false;
        }

        if (size > 0)
        {
            file.write(reinterpret_cast<const char*>(data), static_cast<std::streamsize>(size));
        }

        return file.good();
    }
    
    bool GetLastTimestamp(const String& path, uint64* out_timestamp)
    {
        std::error_code error;
        const std::filesystem::path fs_path = std::filesystem::u8path(path);
        const std::filesystem::file_time_type last_write_time = std::filesystem::last_write_time(fs_path, error);
        if (error)
        {
            return false;
        }
        // seconds unit precision
        *out_timestamp = std::chrono::duration_cast<std::chrono::duration<uint64>>(last_write_time.time_since_epoch()).count();
        return true;
    }

    bool EnumerateDirectoryRecursive(const String& root_path, Vector<DirectoryEntry>* out_entries)
    {
        if (out_entries == nullptr)
        {
            return false;
        }

        out_entries->clear();

        std::error_code error;
        const std::filesystem::path fs_root_path = std::filesystem::absolute(std::filesystem::u8path(root_path), error).lexically_normal();
        if (error || !std::filesystem::is_directory(fs_root_path, error))
        {
            return false;
        }

        std::filesystem::recursive_directory_iterator it(fs_root_path, std::filesystem::directory_options::skip_permission_denied, error);
        std::filesystem::recursive_directory_iterator end;
        for (; !error && it != end; it.increment(error))
        {
            std::error_code status_error;
            const std::filesystem::file_status status = std::filesystem::status(it->path(), status_error);
            if (status_error)
            {
                continue;
            }

            DirectoryEntry entry = {};
            entry.path = it->path().generic_string();
            entry.is_directory = status.type() == std::filesystem::file_type::directory;
            entry.is_file = status.type() == std::filesystem::file_type::regular;
            out_entries->push_back(entry);
        }

        return true;
    }

    std::unique_ptr<DirectoryWatcher> CreateDirectoryWatcher(const String& directory_path, bool recursive)
    {
        std::unique_ptr<DirectoryWatcherInternal> watcher = std::make_unique<DirectoryWatcherInternal>(directory_path, recursive);
        return watcher->IsValid() ? std::move(watcher) : nullptr;
    }

    String GetWorkingDirectory()
    {
        return std::filesystem::current_path().u8string();
    }

    String GetExecutableDirectory()
    {
#if defined(_WIN32)
        char str[1024] = {};
        GetModuleFileNameA(NULL, str, arraysize(str));
        std::filesystem::path executable_path = std::filesystem::u8path(str);
        return executable_path.parent_path().u8string();
#else
        return String();
#endif // _WIN32
        
    }

    bool IsDirectory(const String& path)
    {
        std::error_code error;
        return std::filesystem::is_directory(std::filesystem::u8path(path), error);
    }

    bool IsFile(const String& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(std::filesystem::u8path(path), error);
    }

    String GetExtension(const String& path)
    {
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        String extension = fs_path.extension().u8string();
        if (!extension.empty() && extension.front() == '.')
        {
            extension.erase(extension.begin());
        }
        return extension;
    }

    String ReplaceExtension(const String& path, const String& ext)
    {
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        return fs_path.replace_extension(ext).string();
    }

    String GetFilename(const String& path)
    {
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        return fs_path.filename().u8string();
    }

    String GetDirectoryFromPath(const String& path)
    {
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        if (IsDirectory(path))
        {
            return fs_path.lexically_normal().u8string();
        }
        if (!path.empty() && (path.back() == '/' || path.back() == '\\'))
        {
            return fs_path.lexically_normal().u8string();
        }
        return fs_path.parent_path().u8string();
    }

    String GetRelativePath(const String& root_path, const String& path)
    {
        std::error_code error;
        const std::filesystem::path fs_root_path = std::filesystem::absolute(std::filesystem::u8path(root_path), error).lexically_normal();
        if (error)
        {
            return String();
        }

        const std::filesystem::path fs_path = std::filesystem::absolute(std::filesystem::u8path(path), error).lexically_normal();
        if (error)
        {
            return String();
        }

        std::filesystem::path relative_path = fs_path.lexically_relative(fs_root_path);
        String relative = relative_path.generic_string();
        if (relative.empty() || relative == "." || relative.rfind("..", 0) == 0)
        {
            return String();
        }

        return relative;
    }

    bool CreateFolder(const String& path)
    {
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        return std::filesystem::create_directories(fs_path);
    }
}
