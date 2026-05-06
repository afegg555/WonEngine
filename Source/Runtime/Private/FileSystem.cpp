#include "FileSystem.h"
#include "Platform.h"

#include <chrono>
#include <filesystem>
#include <fstream>

namespace won::io
{
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
