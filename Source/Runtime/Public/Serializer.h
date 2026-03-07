#pragma once
#include "Archive.h"
#include <type_traits>
#include <utility>

namespace won::serialize
{
    // https://stackoverflow.com/questions/34402126/member-detection-using-void-t

    template<typename T, typename = void>
    struct HasSerializeMember : std::false_type
    {
    };

    template<typename T>
    struct HasSerializeMember<T, std::void_t<decltype(std::declval<T&>().Serialize(std::declval<Archive&>()))>> : std::true_type
    {
    };

    template<typename T>
    void Serialize(Archive& archive, T& value)
    {
        if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T>)
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

    inline void Serialize(Archive& archive, String& value)
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

    inline void Serialize(Archive& archive, const String& value)
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
    void Serialize(Archive& archive, Vector<T>& values)
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
    void Serialize(Archive& archive, const Vector<T>& values)
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
