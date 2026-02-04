#include "FileSystem.h"

#include <filesystem>
#include <fstream>

#if defined(_WIN32)
#include <Windows.h>
#endif // _WIN32

namespace won::io
{
    bool FileSystem::Exists(const std::string& path)
    {
        std::error_code error;
        return std::filesystem::exists(std::filesystem::u8path(path), error);
    }

    bool FileSystem::CreateDirectories(const std::string& path)
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

    bool FileSystem::ReadAllBytes(const std::string& path, FileData* out_data)
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

        std::ifstream::pos_type size = file.tellg();
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

    bool FileSystem::WriteAllBytes(const std::string& path, const uint8* data, Size size)
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

        std::ofstream file(fs_path, std::ios::binary | std::ios::trunc);
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

    std::string FileSystem::GetWorkingDirectory()
    {
        return std::filesystem::current_path().u8string();
    }

    std::string FileSystem::GetExecutableDirectory()
    {
#if defined(_WIN32)
        char str[1024] = {};
        GetModuleFileNameA(NULL, str, arraysize(str));
        return str;
#endif // _WIN32
        return std::string();
    }

    bool IsDirectory(const std::string& path)
    {
        std::error_code error;
        return std::filesystem::is_directory(std::filesystem::u8path(path), error);
    }

    bool IsFile(const std::string& path)
    {
        std::error_code error;
        return std::filesystem::is_regular_file(std::filesystem::u8path(path), error);
    }

    std::string GetExtension(const std::string& path)
    {
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        std::string extension = fs_path.extension().u8string();
        if (!extension.empty() && extension.front() == '.')
        {
            extension.erase(extension.begin());
        }
        return extension;
    }

    std::string GetFilename(const std::string& path)
    {
        std::filesystem::path fs_path = std::filesystem::u8path(path);
        return fs_path.filename().u8string();
    }
}
