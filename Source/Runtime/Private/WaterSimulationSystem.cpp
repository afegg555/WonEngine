#include "WaterSimulationSystem.h"
#include "Scene.h"
#include "PhysicsWorld.h"

namespace won::ecs
{
    void WaterSimulationSystem::Update(Scene& scene, float delta_time)
    {
        Scene::WaterSimulationState& water = scene.GetWaterSimulation();

        const physics::PhysicsWorld* physics_world = scene.GetPhysicsWorld();
        if (!physics_world)
        {
            return;
        }

        const double step_seconds = static_cast<double>(physics_world->GetFixedStepSeconds()); //
        const uint32 max_steps = static_cast<uint32>((std::max)(1, physics_world->GetMaxStepsPerFrame()));

        water.step_accumulator += static_cast<double>(delta_time);
        uint32 steps = 0;
        while (water.step_accumulator >= step_seconds && steps < max_steps)
        {
            water.step_accumulator -= step_seconds;
            ++steps;
        }
        if (steps == max_steps)
        {
            water.step_accumulator = 0.0;
        }

        water.pending_steps = steps;
        water.step_count += steps;

        // !! currently we don't have cpu simulation yet
    }
}
