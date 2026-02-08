#pragma once
#include "Scene.h"
#include "Types.h"

namespace won::rendering
{
    struct Viewport
    {
        int32 x = 0;
        int32 y = 0;
        int32 width = 0;
        int32 height = 0;
    };

    struct View
    {
        ecs::Entity camera_entity = {};
        ecs::Scene* scene = nullptr;
        Viewport viewport = {};
    };
}
