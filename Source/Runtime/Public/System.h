#pragma once

namespace won::ecs
{
    class Scene;

    class System
    {
    public:
        virtual ~System() = default;
        virtual void Update(Scene& scene, float delta_time) = 0;
    };
}
