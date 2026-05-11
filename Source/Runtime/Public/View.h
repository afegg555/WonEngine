#pragma once
#include "Scene.h"
#include "Types.h"

namespace won::rendering
{
    enum class RenderPathType
    {
        Forward
    };

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
        RenderPathType render_path_type = RenderPathType::Forward;
        Rect viewport = {};
        Rect scissor = {};

        void Update(float dt)
        {
            if (scene)
            {
                scene->Update(dt);
            }
        }

        bool RayCast(float2 screen_position, ecs::RayCastHit& out_hit, bool use_local_bvh = true) const
        {
            out_hit = {};
            if (!scene || viewport.width <= 0 || viewport.height <= 0)
            {
                return false;
            }

            ecs::CameraComponent* camera = scene->GetComponent<ecs::CameraComponent>(camera_entity);
            if (!camera)
            {
                return false;
            }

            const float viewport_x = static_cast<float>(viewport.x);
            const float viewport_y = static_cast<float>(viewport.y);
            const float viewport_width = static_cast<float>(viewport.width);
            const float viewport_height = static_cast<float>(viewport.height);
            if (screen_position.x < viewport_x || screen_position.y < viewport_y ||
                screen_position.x > viewport_x + viewport_width || screen_position.y > viewport_y + viewport_height)
            {
                return false;
            }

            const float viewport_u = (screen_position.x - viewport_x) / viewport_width;
            const float viewport_v = (screen_position.y - viewport_y) / viewport_height;
            const float ndc_x = viewport_u * 2.0f - 1.0f;
            const float ndc_y = 1.0f - viewport_v * 2.0f;
            const XMMATRIX inv_view_projection = XMLoadFloat4x4(&camera->inv_view_projection);
            const XMVECTOR near_position = XMVector3TransformCoord(XMVectorSet(ndc_x, ndc_y, 1.0f, 1.0f), inv_view_projection);
            const XMVECTOR far_position = XMVector3TransformCoord(XMVectorSet(ndc_x, ndc_y, 0.0f, 1.0f), inv_view_projection);

            math::Ray ray = {};
            if (camera->IsOrtho())
            {
                XMStoreFloat3(&ray.origin, near_position);
                XMStoreFloat3(&ray.direction, XMVector3Normalize(far_position - near_position));
            }
            else
            {
                ray.origin = camera->eye;
                XMStoreFloat3(&ray.direction, XMVector3Normalize(far_position - XMLoadFloat3(&camera->eye)));
            }

            return scene->RayCastClosest(ray, out_hit, use_local_bvh);
        }
    };
}
