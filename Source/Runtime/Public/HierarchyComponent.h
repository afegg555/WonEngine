#pragma once
#include "Types.h"
#include "Entity.h"

namespace won::ecs
{
    struct HierarchyComponent
    {
        Entity parent_id = INVALID_ENTITY;
    };
}
