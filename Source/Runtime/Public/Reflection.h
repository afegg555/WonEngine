#pragma once
#include "RuntimeExport.h"
#include "Types.h"
#include "ReflectionTypes.h"

namespace won::reflection
{
    WONENGINE_API bool RegisterType(const won::TypeDesc* type_desc);
    WONENGINE_API bool UnregisterType(won::TypeId type_id);
    WONENGINE_API const won::TypeDesc* FindType(won::TypeId type_id);
    WONENGINE_API const won::TypeDesc* FindType(StringView name);
    WONENGINE_API const Vector<const won::TypeDesc*>& GetTypes();
    WONENGINE_API void ClearRegistry();
}
