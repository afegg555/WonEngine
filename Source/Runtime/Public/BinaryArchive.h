#pragma once
#include "ArchiveMode.h"
#include "Types.h"
#include "FileSystem.h"
#include <cassert>
#include <cstring>
#include <type_traits>
#include <utility>

namespace won::serialize
{
    class BinaryArchive
    {
    public:
        BinaryArchive(const String& file_name, ArchiveMode mode = ArchiveMode::Write, Size reserve_size = 0)
            : file_name(file_name)
            , mode(mode)
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

        ~BinaryArchive()
        {
            if (IsWriteMode())
            {
                WriteToFile();
            }
        }

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

        void SerializeBytes(void* data, Size size)
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
        String file_name;
        ArchiveMode mode;
        Vector<uint8> bytes;
        Size offset = 0;
    };

    // https://stackoverflow.com/questions/34402126/member-detection-using-void-t

    template<typename T, typename = void>
    struct HasSerializeMember : std::false_type
    {
    };

    template<typename T>
    struct HasSerializeMember<T, std::void_t<decltype(std::declval<T&>().Serialize(std::declval<BinaryArchive&>()))>> : std::true_type
    {
    };

    template<typename T>
    void Serialize(BinaryArchive& archive, T& value)
    {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_trivially_copyable_v<T>)
        {
            archive.SerializeBytes(&value, sizeof(T));
        }
        else if constexpr (HasSerializeMember<T>::value)
        {
            value.Serialize(archive);
        }
        else
        {
            static_assert(HasSerializeMember<T>::value, "Type is not serializable.");
        }
    }

    inline void Serialize(BinaryArchive& archive, String& value)
    {
        Size size = archive.IsWriteMode() ? value.size() : 0;
        Serialize(archive, size); // get or set size bytes

        if (size == 0)
        {
            return;
        }

        if (archive.IsWriteMode())
        {
            archive.SerializeBytes(value.data(), size);
        }
        else
        {
            value.resize(size);
            archive.SerializeBytes(value.data(), size);
        }
    }

    inline void Serialize(BinaryArchive& archive, const String& value)
    {
        Size size = value.size();
        if (size == 0 || archive.IsReadMode())
        {
            return;
        }
        Serialize(archive, size); // set size bytes
        archive.SerializeBytes((void*)value.data(), size);
    }

    template<typename T>
    void Serialize(BinaryArchive& archive, Vector<T>& values)
    {
        Size count = archive.IsWriteMode() ? values.size() : 0;
        Serialize(archive, count); // get or set size bytes
        if (archive.IsWriteMode())
        {
            for (Size i = 0; i < count; ++i)
            {
                Serialize(archive, values[i]);
            }
        }
        else
        {
            values.resize(count);
            for (Size i = 0; i < count; ++i)
            {
                Serialize(archive, values[i]);
            }
        }
    }

    template<typename T>
    void Serialize(BinaryArchive& archive, const Vector<T>& values)
    {
        Size count = values.size();
        if (count == 0 || archive.IsReadMode())
        {
            return;
        }
        Serialize(archive, count); // set size bytes
        for (Size i = 0; i < count; ++i)
        {
            Serialize(archive, values[i]);
        }
    }
}
