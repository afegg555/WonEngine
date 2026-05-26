#include "Reflection.h"

#include "StringUtils.h"

namespace won::reflection
{
    namespace
    {
        Vector<const won::TypeDesc*> types;
    }

    bool RegisterType(const won::TypeDesc* type_desc)
    {
        if (!type_desc || !type_desc->name || type_desc->name[0] == '\0')
        {
            return false;
        }

        const TypeId type_id = utils::Hash(type_desc->name);
        if (type_id == 0)
        {
            return false;
        }

        for (const won::TypeDesc*& type : types)
        {
            if (!type)
            {
                continue;
            }

            if (utils::Hash(type->name) == type_id || StringView(type->name) == type_desc->name)
            {
                type = type_desc;
                return true;
            }
        }

        types.push_back(type_desc);
        return true;
    }

    bool UnregisterType(TypeId type_id)
    {
        if (type_id == 0)
        {
            return false;
        }

        for (Size i = 0; i < types.size(); ++i)
        {
            const won::TypeDesc* type = types[i];
            if (!type || utils::Hash(type->name) != type_id)
            {
                continue;
            }

            types.erase(types.begin() + i);
            return true;
        }

        return false;
    }

    const won::TypeDesc* FindType(TypeId type_id)
    {
        if (type_id == 0)
        {
            return nullptr;
        }

        for (const won::TypeDesc* type : types)
        {
            if (type && utils::Hash(type->name) == type_id)
            {
                return type;
            }
        }

        return nullptr;
    }

    const won::TypeDesc* FindType(StringView name)
    {
        if (name.empty())
        {
            return nullptr;
        }

        for (const won::TypeDesc* type : types)
        {
            if (type && StringView(type->name) == name)
            {
                return type;
            }
        }

        return nullptr;
    }

    const Vector<const won::TypeDesc*>& GetTypes()
    {
        return types;
    }

    void ClearRegistry()
    {
        types.clear();
    }
}
