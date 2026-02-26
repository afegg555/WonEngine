#pragma once
#include "JobSystem.h"
#include "SceneComponents.h"
#include "Entity.h"
#include "Types.h"

using namespace won::jobsystem;
namespace won::ecs
{
    class Scene;

    class System
    {
    public:
        virtual ~System() = default;
        virtual ComponentMask GetReadMask() const { return 0; }
        virtual ComponentMask GetWriteMask() const { return 0; }
        virtual void Update(Scene& scene, float delta_time) = 0;
    };
}
