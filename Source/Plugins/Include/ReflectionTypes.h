#pragma once
#include "PluginABI.h"
#include "StableHash.h"

#include <stdint.h>

namespace won
{
    using TypeId = uint64_t;
    using FieldId = uint64_t;

    enum class ValueType : uint32_t
    {
        Unknown = 0,
        Bool,

        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,

        Float32,
        Float64,

        Int32x2,
        Int32x3,
        Int32x4,

        UInt32x2,
        UInt32x3,
        UInt32x4,

        Float32x2,
        Float32x3,
        Float32x4,

        String,
        Pointer,
        CustomStruct,
        Enum,
    };

    enum FieldFlags : uint32_t
    {
        FieldFlagNone = 0,
        FieldFlagEditable = 1 << 0,
        FieldFlagSerializable = 1 << 1,
        FieldFlagHidden = 1 << 2,
    };

    struct EnumValueDesc
    {
        const char* name;
        int64_t value;
    };

    struct FieldDesc
    {
        uint32_t struct_size;
        FieldId field_id;
        const char* name;
        const char* display_name;
        ValueType value_type;
        const char* type_name;
        uint32_t offset;
        uint32_t size;
        uint32_t flags;
    };

    using ConstructFn = void (WON_PLUGIN_CALL*)(void* memory);
    using DestructFn = void (WON_PLUGIN_CALL*)(void* memory);
    using CopyFn = void (WON_PLUGIN_CALL*)(void* dst, const void* src);

    struct TypeDesc
    {
        uint32_t struct_size;
        TypeId type_id;
        const char* name;
        const char* display_name;
        ValueType value_type;
        uint32_t size;
        uint32_t alignment;
        ConstructFn Construct;
        DestructFn Destruct;
        CopyFn Copy;
        const FieldDesc* fields;
        uint32_t field_count;
        const EnumValueDesc* enum_values;
        uint32_t enum_value_count;
    };
}
