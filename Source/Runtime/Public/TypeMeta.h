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

    template <typename T>
    struct FieldTypeInfo
    {
        static constexpr won::ValueType GetValueType()
        {
            static_assert(TypeMeta<T>::reflected, "WON_REFLECT_FIELD requires a reflected field type.");
            if constexpr (TypeMeta<T>::reflected)
            {
                static_assert(TypeMeta<T>::desc.value_type != won::ValueType::CustomStruct, "Nested custom struct fields are not supported.");
                return TypeMeta<T>::desc.value_type;
            }
            return won::ValueType::Unknown;
        }

        static constexpr won::TypeId GetTypeId()
        {
            if constexpr (TypeMeta<T>::reflected)
            {
                return TypeMeta<T>::type_id;
            }
            return 0;
        }

        inline static constexpr won::ValueType value_type = GetValueType();
        inline static constexpr const won::ArrayDesc* array_desc = nullptr;
        inline static constexpr won::TypeId type_id = GetTypeId();
    };

    template <typename Element>
    struct FieldTypeInfo<won::Vector<Element>>
    {
        static uint32_t GetSize(const void* array)
        {
            return static_cast<uint32_t>(static_cast<const won::Vector<Element>*>(array)->size());
        }

        static void Resize(void* array, uint32_t size)
        {
            static_cast<won::Vector<Element>*>(array)->resize(size);
        }

        static void* GetElement(void* array, uint32_t index)
        {
            won::Vector<Element>& values = *static_cast<won::Vector<Element>*>(array);
            return index < values.size() ? &values[index] : nullptr;
        }

        static const void* GetConstElement(const void* array, uint32_t index)
        {
            const won::Vector<Element>& values = *static_cast<const won::Vector<Element>*>(array);
            return index < values.size() ? &values[index] : nullptr;
        }

        static constexpr won::TypeId GetElementTypeId()
        {
            static_assert(TypeMeta<Element>::reflected, "Reflected arrays require a reflected element type.");
            if constexpr (TypeMeta<Element>::reflected)
            {
                return TypeMeta<Element>::type_id;
            }
            return 0;
        }

        inline static constexpr won::ArrayDesc desc = { sizeof(won::ArrayDesc), GetElementTypeId(), &GetSize, &Resize, &GetElement, &GetConstElement };
        inline static constexpr won::ValueType value_type = won::ValueType::Array;
        inline static constexpr const won::ArrayDesc* array_desc = &desc;
        inline static constexpr won::TypeId type_id = 0;
    };

    template <typename Element, size_t Count>
    struct FieldTypeInfo<Element[Count]>
    {
        static uint32_t GetSize(const void*)
        {
            return static_cast<uint32_t>(Count);
        }

        static void* GetElement(void* array, uint32_t index)
        {
            return index < Count ? static_cast<Element*>(array) + index : nullptr;
        }

        static const void* GetConstElement(const void* array, uint32_t index)
        {
            return index < Count ? static_cast<const Element*>(array) + index : nullptr;
        }

        static constexpr won::TypeId GetElementTypeId()
        {
            static_assert(TypeMeta<Element>::reflected, "Reflected arrays require a reflected element type.");
            if constexpr (TypeMeta<Element>::reflected)
            {
                return TypeMeta<Element>::type_id;
            }
            return 0;
        }

        inline static constexpr won::ArrayDesc desc = { sizeof(won::ArrayDesc), GetElementTypeId(), &GetSize, nullptr, &GetElement, &GetConstElement };
        inline static constexpr won::ValueType value_type = won::ValueType::Array;
        inline static constexpr const won::ArrayDesc* array_desc = &desc;
        inline static constexpr won::TypeId type_id = 0;
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

#define WON_REFLECT_FIELD(member, flags_value) \
        { sizeof(won::FieldDesc), won::StableHash(#member), #member, FieldTypeInfo<decltype(std::declval<ReflectedType>().member)>::value_type, FieldTypeInfo<decltype(std::declval<ReflectedType>().member)>::array_desc, static_cast<uint32_t>(offsetof(ReflectedType, member)), static_cast<uint32_t>(sizeof(std::declval<ReflectedType>().member)), flags_value, nullptr, 0, FieldTypeInfo<decltype(std::declval<ReflectedType>().member)>::type_id },

#define WON_REFLECT_STRUCT_END() \
        { sizeof(won::FieldDesc), 0, "", ValueType::Unknown, nullptr, 0, 0, FieldFlagNone, nullptr, 0, 0 } \
    }; \
    static void Construct(void* memory) { new (memory) ReflectedType(); } \
    static void Destruct(void* memory) { static_cast<ReflectedType*>(memory)->~ReflectedType(); } \
    static void Copy(void* dst, const void* src) { new (dst) ReflectedType(*static_cast<const ReflectedType*>(src)); } \
    inline static constexpr won::TypeDesc desc = { sizeof(won::TypeDesc), type_id, type_name, display_name, reflected_value_type, static_cast<uint32_t>(sizeof(ReflectedType)), static_cast<uint32_t>(alignof(ReflectedType)), &Construct, &Destruct, &Copy, fields, static_cast<uint32_t>(sizeof(fields) / sizeof(fields[0]) - 1), nullptr, 0 }; \
    static const won::TypeDesc* Get() { return &desc; } \
};

#define WON_REFLECT_FLAGS_FIELD(member, flags_value, flag_enum_type) \
        { sizeof(won::FieldDesc), won::StableHash(#member), #member, FieldTypeInfo<decltype(std::declval<ReflectedType>().member)>::value_type, FieldTypeInfo<decltype(std::declval<ReflectedType>().member)>::array_desc, static_cast<uint32_t>(offsetof(ReflectedType, member)), static_cast<uint32_t>(sizeof(std::declval<ReflectedType>().member)), flags_value, TypeMeta<flag_enum_type>::enum_values, static_cast<uint32_t>(sizeof(TypeMeta<flag_enum_type>::enum_values) / sizeof(TypeMeta<flag_enum_type>::enum_values[0])), FieldTypeInfo<decltype(std::declval<ReflectedType>().member)>::type_id },

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
