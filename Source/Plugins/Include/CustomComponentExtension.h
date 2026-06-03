#pragma once
#include "PluginABI.h"
#include "ReflectionTypes.h"

#include <stdint.h>

namespace won::plugin::component
{
    inline constexpr WonExtensionType ExtensionType = WonExtensionType::Component;

    using FieldDesc = won::FieldDesc;
    using Desc = won::TypeDesc;
}
