#include "MeshUpdateSystem.h"
#include "Scene.h"
#include "PhysicsWorld.h"
#include "TerrainGenerator.h"
#include "RenderingUtils.h"
#include "JobSystem.h"

using namespace DirectX;

namespace won::ecs
{
    void MeshUpdateSystem::Update(Scene& scene, float delta_time)
    {
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();

        jobsystem::Context ctx;

        if (auto terrain_array = scene.GetComponentArray<TerrainComponent>().get())
        {
            jobsystem::Dispatch(ctx, (uint32_t)terrain_array->GetSize(), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
            {
                const Entity entity = terrain_array->index_to_entity[args.job_index];
                GeometryComponent* geometry = scene.GetComponent<GeometryComponent>(entity);
                if (!geometry || geometry->mesh)
                {
                    return;
                }

                auto mesh = GenerateTerrainMesh(terrain_array->data[args.job_index]);
                geometry->SetMesh(mesh);
                rendering::utils::EnqueueResourceUpload(mesh);
            });
            jobsystem::Wait(ctx);
        }

        jobsystem::Wait(ctx);
    }
}
