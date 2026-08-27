#include "MeshUpdateSystem.h"
#include "Scene.h"
#include "PhysicsWorld.h"
#include "TerrainGenerator.h"
#include "SoftBodyGenerator.h"
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

        auto soft_body_array = scene.GetComponentArray<SoftBodyComponent>().get();
        if (!soft_body_array)
        {
            return;
        }

        physics::PhysicsWorld* physics_world = scene.GetPhysicsWorld();
        jobsystem::Dispatch(ctx, (uint32_t)soft_body_array->GetSize(), jobsystem::groupsize_heavy, [&](jobsystem::JobArgs args)
        {
            SoftBodyComponent& soft_body = soft_body_array->data[args.job_index];
            const Entity entity = soft_body_array->index_to_entity[args.job_index];
            if (!soft_body.IsEnabled())
            {
                return;
            }

            GeometryComponent* geometry = scene.GetComponent<GeometryComponent>(entity);
            if (!geometry)
            {
                return;
            }

            if (!geometry->mesh || soft_body.IsTopologyDirty())
            {
                rendering::utils::EnqueueMeshRelease(geometry->mesh);

                auto generated = GenerateSoftBodyMesh(soft_body);
                geometry->SetMesh(generated);
                rendering::utils::EnqueueResourceUpload(generated);

                soft_body.SetTopologyDirty(false);
                soft_body.SetDirty();
                return;
            }

            if (!physics_world || !physics_world->HasBody(entity) || !transform_array || !transform_array->HasData(entity))
            {
                return;
            }

            const uint32 vertices_x = (std::max)(1u, soft_body.divisions_x) + 1u;
            const uint32 vertices_y = (std::max)(1u, soft_body.divisions_y) + 1u;
            const Size vertex_count = static_cast<Size>(vertices_x) * vertices_y;

            resource::Mesh& mesh = *geometry->mesh;
            if (mesh.positions.size() != vertex_count || mesh.normals.size() != vertex_count)
            {
                return;
            }

            physics_world->GetSoftBodyVertices(entity, mesh.positions);
            if (mesh.positions.size() != vertex_count)
            {
                return;
            }

            const XMMATRIX to_local = XMMatrixInverse(nullptr, transform_array->GetData(entity).GetWorldTransform());
            for (Size vertex_index = 0; vertex_index < vertex_count; ++vertex_index)
            {
                XMStoreFloat3(&mesh.positions[vertex_index], XMVector3TransformCoord(XMLoadFloat3(&mesh.positions[vertex_index]), to_local));
            }

            rendering::utils::EnqueueVertexStreamUpdate(geometry->mesh);
        });
        jobsystem::Wait(ctx);
    }
}
