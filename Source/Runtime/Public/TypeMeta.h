#pragma once
#include "Types.h"
#include "ReflectionTypes.h"

#include <cstddef>
#include <new>
#include <utility>

namespace won::reflection
{
    template<typename T>
    struct TypeMeta
    {
        inline static constexpr bool reflected = false;
        static const won::TypeDesc* Get() { return nullptr; }
    };
}

#define WON_REFLECT_SCALAR(type, display_name_value, value_type_value) \
template<> struct TypeMeta<type> { \
    using ReflectedType = type; \
    inline static constexpr bool reflected = true; \
    inline static constexpr won::TypeId type_id = won::StableHash(#type); \
    inline static constexpr won::TypeDesc desc = { sizeof(won::TypeDesc), type_id, #type, display_name_value, value_type_value, static_cast<uint32_t>(sizeof(ReflectedType)), static_cast<uint32_t>(alignof(ReflectedType)), nullptr, nullptr, nullptr, nullptr, 0, nullptr, 0 }; \
    static const won::TypeDesc* Get() { return &desc; } \
};

#define WON_REFLECT_STRUCT(type, display_name_value) \
template<> struct TypeMeta<type> { \
    using ReflectedType = type; \
    inline static constexpr bool reflected = true; \
    inline static constexpr const char* type_name = #type; \
    inline static constexpr won::TypeId type_id = won::StableHash(type_name); \
    inline static constexpr const char* display_name = display_name_value; \
    inline static constexpr won::ValueType reflected_value_type = ValueType::CustomStruct; \
    inline static constexpr won::FieldDesc fields[] = {

#define WON_REFLECT_BUILTIN_STRUCT(type, display_name_value, value_type_value) \
template<> struct TypeMeta<type> { \
    using ReflectedType = type; \
    inline static constexpr bool reflected = true; \
    inline static constexpr const char* type_name = #type; \
    inline static constexpr won::TypeId type_id = won::StableHash(type_name); \
    inline static constexpr const char* display_name = display_name_value; \
    inline static constexpr won::ValueType reflected_value_type = value_type_value; \
    inline static constexpr won::FieldDesc fields[] = {

#define WON_REFLECT_FIELD(member, value_type_value, flags_value) \
        { sizeof(won::FieldDesc), won::StableHash(#member), #member, #member, value_type_value, "", static_cast<uint32_t>(offsetof(ReflectedType, member)), static_cast<uint32_t>(sizeof(std::declval<ReflectedType>().member)), flags_value },

#define WON_REFLECT_FIELD_TYPED(member, field_type, value_type_value, flags_value) \
        { sizeof(won::FieldDesc), won::StableHash(#member), #member, #member, value_type_value, #field_type, static_cast<uint32_t>(offsetof(ReflectedType, member)), static_cast<uint32_t>(sizeof(std::declval<ReflectedType>().member)), flags_value },

#define WON_REFLECT_STRUCT_END() \
        { sizeof(won::FieldDesc), 0, "", "", ValueType::Unknown, "", 0, 0, FieldFlagNone } \
    }; \
    static void Construct(void* memory) { new (memory) ReflectedType(); } \
    static void Destruct(void* memory) { static_cast<ReflectedType*>(memory)->~ReflectedType(); } \
    static void Copy(void* dst, const void* src) { new (dst) ReflectedType(*static_cast<const ReflectedType*>(src)); } \
    inline static constexpr won::TypeDesc desc = { sizeof(won::TypeDesc), type_id, type_name, display_name, reflected_value_type, static_cast<uint32_t>(sizeof(ReflectedType)), static_cast<uint32_t>(alignof(ReflectedType)), &Construct, &Destruct, &Copy, fields, static_cast<uint32_t>(sizeof(fields) / sizeof(fields[0]) - 1), nullptr, 0 }; \
    static const won::TypeDesc* Get() { return &desc; } \
};

#define WON_REFLECT_ENUM(type, display_name_value) \
template<> struct TypeMeta<type> { \
    using ReflectedType = type; \
    inline static constexpr bool reflected = true; \
    inline static constexpr const char* type_name = #type; \
    inline static constexpr won::TypeId type_id = won::StableHash(type_name); \
    inline static constexpr const char* display_name = display_name_value; \
    inline static constexpr won::EnumValueDesc enum_values[] = {

#define WON_REFLECT_ENUM_VALUE(name_value, value_expression) \
        { name_value, static_cast<int64>(value_expression) },

#define WON_REFLECT_ENUM_END() \
    }; \
    inline static constexpr won::TypeDesc desc = { sizeof(won::TypeDesc), type_id, type_name, display_name, ValueType::Enum, static_cast<uint32_t>(sizeof(ReflectedType)), static_cast<uint32_t>(alignof(ReflectedType)), nullptr, nullptr, nullptr, nullptr, 0, enum_values, static_cast<uint32_t>(sizeof(enum_values) / sizeof(enum_values[0])) }; \
    static const won::TypeDesc* Get() { return &desc; } \
};
