#pragma once
#include "Scene.h"
#include "Types.h"

namespace won::rendering
{
    struct Rect
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
        Rect viewport = {};
        Rect scissor = {};

        void Update(float dt)
        {
            if (scene)
            {
                scene->Update(dt);
            }
        }
    };
}
