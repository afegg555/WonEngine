#pragma once
#include "ArchiveMode.h"
#include "RuntimeExport.h"
#include "Types.h"

#include <memory>

namespace won::serialize
{
    struct JsonArchiveDesc
    {
        bool pretty = true; // write indented JSON
    };

    class WONENGINE_API JsonArchive
    {
    public:
        JsonArchive(ArchiveMode mode, const JsonArchiveDesc& desc = {});
        ~JsonArchive();

        JsonArchive(const JsonArchive&) = delete;
        JsonArchive& operator=(const JsonArchive&) = delete;

        bool IsReadMode() const;
        bool IsWriteMode() const;

        bool LoadFromFile(const String& path);
        bool SaveToFile(const String& path) const;

        bool HasError() const;
        const String& GetError() const;

        bool BeginObject(const char* name = nullptr);
        void EndObject();

        bool BeginArray(const char* name = nullptr);
        void EndArray();

        bool BeginItem();
        void EndItem();

        Size GetArraySize() const;
        Vector<String> GetObjectKeys() const;
        bool HasField(const char* name) const;
        bool FieldToString(const char* name, String& out_value) const;

        bool BeginField(const char* name);
        void EndField();

        template <typename T>
        bool Field(const char* name, T& value)
        {
            if (!BeginField(name))
            {
                return false;
            }
            Serialize(*this, value);
            EndField();
            return !HasError();
        }

        template <typename T>
        bool Field(const char* name, const T& value)
        {
            T copy = value;
            return Field(name, copy);
        }

        template <typename T>
        bool Item(T& value)
        {
            if (!BeginItem())
            {
                return false;
            }
            Serialize(*this, value);
            EndItem();
            return !HasError();
        }

        template <typename T>
        bool Item(const T& value)
        {
            T copy = value;
            return Item(copy);
        }

        bool Value(bool& value);
        bool Value(int8& value);
        bool Value(uint8& value);
        bool Value(int16& value);
        bool Value(uint16& value);
        bool Value(int32& value);
        bool Value(uint32& value);
        bool Value(int64& value);
        bool Value(uint64& value);
        bool Value(float& value);
        bool Value(double& value);
        bool Value(String& value);

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };

    WONENGINE_API void Serialize(JsonArchive& archive, bool& value);
    WONENGINE_API void Serialize(JsonArchive& archive, int8& value);
    WONENGINE_API void Serialize(JsonArchive& archive, uint8& value);
    WONENGINE_API void Serialize(JsonArchive& archive, int16& value);
    WONENGINE_API void Serialize(JsonArchive& archive, uint16& value);
    WONENGINE_API void Serialize(JsonArchive& archive, int32& value);
    WONENGINE_API void Serialize(JsonArchive& archive, uint32& value);
    WONENGINE_API void Serialize(JsonArchive& archive, int64& value);
    WONENGINE_API void Serialize(JsonArchive& archive, uint64& value);
    WONENGINE_API void Serialize(JsonArchive& archive, float& value);
    WONENGINE_API void Serialize(JsonArchive& archive, double& value);
    WONENGINE_API void Serialize(JsonArchive& archive, String& value);
    WONENGINE_API void Serialize(JsonArchive& archive, float2& value);
    WONENGINE_API void Serialize(JsonArchive& archive, float3& value);
    WONENGINE_API void Serialize(JsonArchive& archive, float4& value);
    WONENGINE_API void Serialize(JsonArchive& archive, int2& value);
    WONENGINE_API void Serialize(JsonArchive& archive, int3& value);
    WONENGINE_API void Serialize(JsonArchive& archive, int4& value);
    WONENGINE_API void Serialize(JsonArchive& archive, uint2& value);
    WONENGINE_API void Serialize(JsonArchive& archive, uint3& value);
    WONENGINE_API void Serialize(JsonArchive& archive, uint4& value);
}
