#include "LightUpdateSystem.h"

#include "MathUtils.h"
#include "Scene.h"
#include "LightComponent.h"
#include "RectPacker.h"
#include "JobSystem.h"
#include "Backlog.h"
#include <mutex>

namespace won::ecs
{
    static std::mutex shadowmap_atlas_mutex;
    constexpr int32 shadow_map_atlas_max_size = 16384;

    void LightUpdateSystem::Update(Scene& scene, float delta_time)
    {
        jobsystem::Context sub_ctx;
        auto light_array = scene.GetComponentArray<LightComponent>().get();
        auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        Scene::RenderData& render_data = scene.GetRenderData();

        Size light_count = light_array->GetSize();
        if (light_array->GetSize() > NUM_MAX_LIGHTS_FORWARD_RENDERING)
        {
            backlog::Post(String("Maximum number of lights in forward rendering is: ") + std::to_string(NUM_MAX_LIGHTS_FORWARD_RENDERING), backlog::LogLevel::Warning);
            light_count = NUM_MAX_LIGHTS_FORWARD_RENDERING;
        }

        render_data.shader_lights.resize(light_count);
        render_data.forward_light_mask = { 0,0,0,0 };
        render_data.shadow_map_atlas_size = { 0, 0 };

        render_data.render_shadow_lights.resize(light_count);

        rectpacker::State shadow_map_atlas_packer = {};

        jobsystem::Dispatch(sub_ctx, (uint32)light_count, groupsize, [&](jobsystem::JobArgs args) {
            LightComponent& light = light_array->data[args.job_index];
            Entity entity = light_array->index_to_entity[args.job_index];

            if (!transform_array->HasData(entity))
            {
                backlog::Post("Light Entity does not have transform component", backlog::LogLevel::Error);
                return;
            }

            const TransformComponent& transform = transform_array->GetData(entity);

            light.position = math::GetPosition(transform.world_transform);
            light.direction = math::GetForward(transform.world_transform);

            switch (light.type)
            {
            case LightComponent::Directional:
                light.aabb.CreateFromHalfWidth(float3(0, 0, 0), float3(FLT_MAX, FLT_MAX, FLT_MAX));
                break;
            case LightComponent::Spot:
                light.aabb.CreateFromHalfWidth(light.position, float3(light.range, light.range, light.range));
                break;
            case LightComponent::Point:
                light.aabb.CreateFromHalfWidth(light.position, float3(light.range, light.range, light.range));
                break;
            default:
                light.aabb.CreateFromHalfWidth(float3(0, 0, 0), float3(0, 0, 0));
                break;
            }

            if (light.IsActive())
            {
                ShaderLight& shader_light = render_data.shader_lights[args.job_index];
                shader_light.Init();
                shader_light.position = light.position;

                shader_light.SetType(light.type);
                shader_light.SetColor({ light.color.x * light.intensity, light.color.y * light.intensity, light.color.z * light.intensity, light.intensity });
                shader_light.SetRange(light.range);
                shader_light.SetDirection(light.direction);
                shader_light.SetOuterConeAngleCos(cos(light.outer_cone_angle));
                shader_light.SetInnerConeAngleCos(cos(light.inner_cone_angle));
                if (!light.IsDynamic()) shader_light.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_LIGHT_STATIC);
                if (light.IsCastShadow()) shader_light.SetFlags(SHADER_LIGHT_FLAGS::LIGHT_FLAG_LIGHT_CASTING_SHADOW);

                const uint8_t bucket_index = uint8_t(args.job_index / 32);
                const uint8_t bucket_place = uint8_t(args.job_index % 32);
                uint32_t* value = reinterpret_cast<uint32_t*>(&render_data.forward_light_mask);
                value[bucket_index] |= 1 << bucket_place;

                if(light.IsDynamic() && light.IsCastShadow())
                {
                    auto& render_shadow_light = render_data.render_shadow_lights[args.job_index];

                    const XMVECTOR light_position = XMLoadFloat3(&light.position);
                    XMVECTOR light_direction = XMVector3Normalize(XMLoadFloat3(&light.direction));
                    XMVECTOR light_up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
                    if (std::abs(XMVectorGetX(XMVector3Dot(light_up, light_direction))) > 0.99f)
                    {
                        light_up = XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
                    }

                    XMMATRIX shadow_view = {};
                    XMMATRIX shadow_projection = {};

                    if (light.type == LightComponent::Directional)
                    {
                        const XMVECTOR shadow_eye = light_position - light_direction * 100.0f;
                        shadow_view = XMMatrixLookToLH(shadow_eye, light_direction, light_up);
                        // TODO: current projection size is hard coded.
                        shadow_projection = XMMatrixOrthographicLH(20.0f, 20.0f, 1000.0f, 0.1f);
                    }
                    else if (light.type == LightComponent::Spot)
                    {
                        shadow_view = XMMatrixLookToLH(light_position, light_direction, light_up);
                        shadow_projection = XMMatrixPerspectiveFovLH((std::max)(0.1f, light.outer_cone_angle * 2.0f), 1.0f, light.range, 0.1f);
                    }
                    else if (light.type == LightComponent::Point)
                    {
                        shadow_view = XMMatrixLookToLH(light_position, light_direction, light_up);
                        shadow_projection = XMMatrixPerspectiveFovLH(XM_PIDIV2, 1.0f, light.range, 0.1f);
                    }
                    else
                    {
                        backlog::Post("Shadowmap is not supported on this light type", backlog::LogLevel::Error);
                        return;
                    }

                    XMMATRIX shadow_view_projection = {};
                    shadow_view_projection = shadow_view * shadow_projection;
                    XMStoreFloat4x4(&render_shadow_light.view_projection, shadow_view_projection);
                    shader_light.shadow_view_projection = render_shadow_light.view_projection;
                    render_shadow_light.shadow_map_resolution = light.shadow_map_resolution;
                    render_shadow_light.light_index = args.job_index;

                    rectpacker::Rect rect = {};
                    rect.id = static_cast<int>(args.job_index);
                    rect.w = static_cast<stbrp_coord>((std::max)(1u, light.shadow_map_resolution));
                    rect.h = static_cast<stbrp_coord>((std::max)(1u, light.shadow_map_resolution));

                    {
                        std::lock_guard<std::mutex> lock(shadowmap_atlas_mutex);
                        shadow_map_atlas_packer.AddRect(rect);
                    }
                    
                }
            }
        });

        jobsystem::Wait(sub_ctx);

        if (!shadow_map_atlas_packer.rects.empty())
        {
            if (!shadow_map_atlas_packer.Pack(shadow_map_atlas_max_size))
            {
                backlog::Post("failed to pack shadow map atlas", backlog::LogLevel::Error);
                return;
            }

            render_data.shadow_map_atlas_size = {
                static_cast<uint32>(shadow_map_atlas_packer.width),
                static_cast<uint32>(shadow_map_atlas_packer.height)
            };

            for (const rectpacker::Rect& rect : shadow_map_atlas_packer.rects)
            {
                if (rect.was_packed == 0 || rect.id < 0)
                {
                    continue;
                }

                auto& render_shadow_light = render_data.render_shadow_lights[rect.id];
                render_shadow_light.shadow_map_atlas_rect = { rect.x, rect.y, rect.w, rect.h };
                ShaderLight& shader_light = render_data.shader_lights[render_shadow_light.light_index];
                shader_light.shadow_atlas_scale_bias = {
                    static_cast<float>(rect.w) / static_cast<float>(render_data.shadow_map_atlas_size.x),
                    static_cast<float>(rect.h) / static_cast<float>(render_data.shadow_map_atlas_size.y),
                    static_cast<float>(rect.x) / static_cast<float>(render_data.shadow_map_atlas_size.x),
                    static_cast<float>(rect.y) / static_cast<float>(render_data.shadow_map_atlas_size.y)
                };
            }
        }
    }
}
