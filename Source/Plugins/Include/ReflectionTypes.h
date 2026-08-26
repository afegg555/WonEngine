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
        Array,
    };

    enum FieldFlags : uint32_t
    {
        FieldFlagNone = 0,
        FieldFlagEditable = 1 << 0,
        FieldFlagSerializable = 1 << 1,
        FieldFlagHidden = 1 << 2,
        FieldFlagEntityRef = 1 << 3,
    };

    struct EnumValueDesc
    {
        const char* name;
        int64_t value;
    };

    using ArrayGetSizeFn = uint32_t (WON_PLUGIN_CALL*)(const void* array);
    using ArrayResizeFn = void (WON_PLUGIN_CALL*)(void* array, uint32_t size);
    using ArrayGetElementFn = void* (WON_PLUGIN_CALL*)(void* array, uint32_t index);
    using ArrayGetConstElementFn = const void* (WON_PLUGIN_CALL*)(const void* array, uint32_t index);

    struct ArrayDesc
    {
        uint32_t struct_size;
        TypeId element_type_id;
        ArrayGetSizeFn GetSize;
        ArrayResizeFn Resize;
        ArrayGetElementFn GetElement;
        ArrayGetConstElementFn GetConstElement;
    };

    // arrays carry their element type through ArrayDesc. Nested custom struct fields are not supported.
    struct FieldDesc
    {
        uint32_t struct_size;
        FieldId field_id;
        const char* name;
        ValueType value_type;
        const ArrayDesc* array_desc;
        uint32_t offset;
        uint32_t size;
        uint32_t flags;
        const EnumValueDesc* flag_values;
        uint32_t flag_value_count;
        TypeId type_id;
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
