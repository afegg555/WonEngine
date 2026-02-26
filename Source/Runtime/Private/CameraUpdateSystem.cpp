#include "CameraUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "TransformComponent.h"
#include "CameraComponent.h"
#include "JobSystem.h"
#include "Backlog.h"

namespace won::ecs
{
    void CameraUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto camera_array = scene.GetComponentArray<CameraComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();

        jobsystem::Dispatch(sub_ctx, (uint32_t)camera_array->GetSize(), groupsize, [&](jobsystem::JobArgs args) {
            CameraComponent& camera = camera_array->data[args.job_index];
            Entity entity = camera_array->index_to_entity[args.job_index];

            if (!transform_array->HasData(entity))
            {
                backlog::Post("Camera Entity does not have transform component", backlog::LogLevel::Error);
                return;
            }

            const TransformComponent& transform = transform_array->GetData(entity);

            camera.eye = math::GetPosition(transform.world_transform);
            camera.forward = math::GetForward(transform.world_transform);
            camera.up = math::GetUp(transform.world_transform);

            XMVECTOR eye = XMLoadFloat3(&camera.eye);
            XMVECTOR forward = XMLoadFloat3(&camera.forward);
            XMVECTOR up = XMLoadFloat3(&camera.up);

            XMMATRIX view = XMMatrixLookToLH(eye, forward, up);
            XMMATRIX inv_view = XMMatrixInverse(nullptr, view);
            
            XMMATRIX projection{};
            if (camera.IsOrtho())
            {
                float ortho_height = camera.ortho_vertical_size;
                float ortho_width = ortho_height * camera.aspect_ratio;
                projection = XMMatrixOrthographicLH(ortho_width, ortho_height, camera.far, camera.near); // reverse zbuffer!
            }
            else
            {
                projection = XMMatrixPerspectiveFovLH(camera.fov_y, camera.aspect_ratio, camera.far, camera.near); // reverse zbuffer!
            }

            XMMATRIX inv_projection = XMMatrixInverse(nullptr, projection);
            XMMATRIX view_projection = XMMatrixMultiply(view, projection);
            XMMATRIX inv_view_projection = XMMatrixInverse(nullptr, view_projection);

            XMStoreFloat4x4(&camera.view, view);
            XMStoreFloat4x4(&camera.projection, projection);
            XMStoreFloat4x4(&camera.inv_projection, inv_projection);
            XMStoreFloat4x4(&camera.view_projection, view_projection);
            XMStoreFloat4x4(&camera.inv_view, inv_view);
            XMStoreFloat4x4(&camera.inv_view_projection, inv_view_projection);
            });

        jobsystem::Wait(sub_ctx);
    }
}
