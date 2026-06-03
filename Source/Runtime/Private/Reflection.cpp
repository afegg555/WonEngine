#include "Reflection.h"

namespace won::reflection
{
    namespace
    {
        Vector<const won::TypeDesc*> types;
    }

    bool RegisterType(const won::TypeDesc* type_desc)
    {
        if (!type_desc || type_desc->type_id == 0 || !type_desc->name || type_desc->name[0] == '\0')
        {
            return false;
        }

        for (const won::TypeDesc*& type : types)
        {
            if (!type)
            {
                continue;
            }

            if (type->type_id == type_desc->type_id)
            {
                type = type_desc;
                return true;
            }

            if (StringView(type->name) == type_desc->name)
            {
                return false;
            }
        }

        types.push_back(type_desc);
        return true;
    }

    bool UnregisterType(won::TypeId type_id)
    {
        if (type_id == 0)
        {
            return false;
        }

        for (Size i = 0; i < types.size(); ++i)
        {
            const won::TypeDesc* type = types[i];
            if (!type || type->type_id != type_id)
            {
                continue;
            }

            types.erase(types.begin() + i);
            return true;
        }

        return false;
    }

    const won::TypeDesc* FindType(won::TypeId type_id)
    {
        if (type_id == 0)
        {
            return nullptr;
        }

        for (const won::TypeDesc* type : types)
        {
            if (type && type->type_id == type_id)
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
