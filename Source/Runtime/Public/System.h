#pragma once
#include "JobSystem.h"
#include "SceneComponents.h"
#include "Entity.h"
#include "Types.h"
#include "RuntimeExport.h"

using namespace won::jobsystem;
namespace won::ecs
{
    class Scene;

    enum class SystemExecutionPolicy
    {
        ParallelJob, // run through the job system
        Synchronous, // run immediately on the caller thread
    };

    enum class SystemPhase : uint32
    {
        PreSimulation  = 0,
        Simulation     = 1,
        PostSimulation = 2,

        Count,
    };

    class WONENGINE_API System
    {
    public:
        virtual ~System() = default;
        virtual ComponentMask GetReadMask() const { return 0; }
        virtual ComponentMask GetWriteMask() const { return 0; }
        virtual SystemExecutionPolicy GetExecutionPolicy() const { return SystemExecutionPolicy::ParallelJob; }
        virtual SystemPhase GetPhase() const { return SystemPhase::PostSimulation; }
        virtual void Update(Scene& scene, float delta_time) = 0;
    };
}
