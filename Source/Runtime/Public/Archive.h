#pragma once
#include "Types.h"
#include "FileSystem.h"
#include <cassert>
#include <cstring>
#include <utility>

namespace won::serialize
{
    enum class ArchiveMode
    {
        Read,
        Write
    };

    class Archive
    {
    public:
        Archive(const String& file_name, ArchiveMode mode = ArchiveMode::Write)
            : file_name(file_name)
            , mode(mode)
        {
        }

        virtual ~Archive() = default;

        bool IsReadMode() const
        {
            return mode == ArchiveMode::Read;
        }

        bool IsWriteMode() const
        {
            return mode == ArchiveMode::Write;
        }

        const String& GetFileName() const
        {
            return file_name;
        }

        virtual void SerializeBytes(void* data, Size size) = 0;

    private:
        String file_name;
        ArchiveMode mode;
    };

    class BinaryArchive final : public Archive
    {
    public:
        explicit BinaryArchive(const String& file_name, ArchiveMode mode = ArchiveMode::Write, Size reserve_size = 0)
            : Archive(file_name, mode)
        {
            if (IsReadMode())
            {
                LoadFromFile();
                return;
            }

            if (reserve_size > 0)
            {
                bytes.reserve(reserve_size);
            }
        }

        ~BinaryArchive() override
        {
            if (IsWriteMode())
            {
                WriteToFile();
            }
        }

        bool LoadFromFile()
        {
            if (IsWriteMode())
            {
                assert(false);
                return false;
            }
            bytes.clear();
            offset = 0;

            io::FileData file_data = {};
            if (!io::ReadAllBytes(GetFileName(), &file_data))
            {
                return false;
            }

            bytes = std::move(file_data.bytes);
            return true;
        }

        bool WriteToFile()
        {
            if (IsReadMode())
            {
                assert(false);
                return false;
            }
            if (!io::WriteAllBytes(GetFileName(), bytes.data(), bytes.size()))
            {
                return false;
            }
            return true;
        }

        void SerializeBytes(void* data, Size size) override
        {
            if (size == 0)
            {
                return;
            }

            if (IsWriteMode())
            {
                const Size required_size = offset + size;
                if (required_size > bytes.size())
                {
                    bytes.resize(required_size);
                }
                std::memcpy(bytes.data() + offset, data, size);
                offset += size;
                return;
            }

            const Size available_size = bytes.size() - offset;
            const Size readable_size = size <= available_size ? size : available_size;
            if (readable_size > 0)
            {
                std::memcpy(data, bytes.data() + offset, readable_size);
                offset += readable_size;
            }
            if (readable_size < size)
            {
                assert(false);
            }
        }

        void Clear()
        {
            bytes.clear();
            offset = 0;
        }

        bool IsEnd() const
        {
            return offset >= bytes.size();
        }

        Size GetRemainingBytes() const
        {
            return offset < bytes.size() ? bytes.size() - offset : 0;
        }

        Size GetOffset() const
        {
            return offset;
        }

        void SetOffset(Size new_offset)
        {
            offset = new_offset <= bytes.size() ? new_offset : bytes.size();
        }

        const Vector<uint8>& GetBytes() const
        {
            return bytes;
        }

    private:
        Vector<uint8> bytes;
        Size offset = 0;
    };
}
