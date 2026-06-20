#include "SpriteUpdateSystem.h"

#include "MaterialComponent.h"
#include "Scene.h"
#include "Sprite2DComponent.h"
#include "Sprite3DComponent.h"
#include "TransformComponent.h"
#include "MathUtils.h"

#include <iterator>

namespace won::ecs
{
    void SpriteUpdateSystem::Update(Scene& scene, float delta_time)
    {
        struct SpriteBucket
        {
            Vector<Scene::RenderData::Sprite2DRenderable> sprite_2d_renderables;
            Vector<Scene::RenderData::Sprite3DRenderable> sprite_3d_renderables;
        };

        jobsystem::Context sub_ctx;

        Scene::RenderData& render_data = scene.GetRenderData();
        const auto sprite_2d_array = scene.GetComponentArray<Sprite2DComponent>().get();
        const auto sprite_3d_array = scene.GetComponentArray<Sprite3DComponent>().get();
        const auto transform_array = scene.GetComponentArray<TransformComponent>().get();
        const auto material_array = scene.GetComponentArray<MaterialComponent>().get();

        render_data.sprite_2d_renderables.clear();
        render_data.sprite_3d_renderables.clear();
        if (!material_array)
        {
            return;
        }

        Vector<SpriteBucket> sprite_2d_buckets;
        Vector<SpriteBucket> sprite_3d_buckets;
        if (sprite_2d_array)
        {
            const uint32 sprite_2d_job_count = static_cast<uint32>(sprite_2d_array->GetSize());
            sprite_2d_buckets.resize(jobsystem::DispatchGroupCount(sprite_2d_job_count, groupsize));
            jobsystem::Dispatch(sub_ctx, sprite_2d_job_count, groupsize, [&](jobsystem::JobArgs args) {
                SpriteBucket& bucket = sprite_2d_buckets[args.group_id];

                const Entity entity = sprite_2d_array->index_to_entity[args.job_index];
                if (!material_array->HasData(entity))
                {
                    return;
                }

                Sprite2DComponent& sprite = sprite_2d_array->data[args.job_index];
                const MaterialComponent& material = material_array->GetData(entity);
                if (material.GetMaterialSlotCount() == 0)
                {
                    return;
                }

                Scene::RenderData::Sprite2DRenderable renderable = {};
                renderable.material_index = material.material_offset;
                renderable.anchor = sprite.anchor;
                renderable.position = sprite.position;
                renderable.size = sprite.size;
                renderable.pivot = sprite.pivot;
                renderable.uv_rect = sprite.uv_rect;
                renderable.layer = sprite.layer;
                bucket.sprite_2d_renderables.push_back(renderable);
                sprite.SetDirty(false);
            });
        }

        if (sprite_3d_array && transform_array)
        {
            const uint32 sprite_3d_job_count = static_cast<uint32>(sprite_3d_array->GetSize());
            sprite_3d_buckets.resize(jobsystem::DispatchGroupCount(sprite_3d_job_count, groupsize));
            jobsystem::Dispatch(sub_ctx, sprite_3d_job_count, groupsize, [&](jobsystem::JobArgs args) {
            SpriteBucket& bucket = sprite_3d_buckets[args.group_id];

            const Entity entity = sprite_3d_array->index_to_entity[args.job_index];
            if (!transform_array->HasData(entity) || !material_array->HasData(entity))
            {
                return;
            }

            Sprite3DComponent& sprite = sprite_3d_array->data[args.job_index];
            const MaterialComponent& material = material_array->GetData(entity);
            if (material.GetMaterialSlotCount() == 0)
            {
                return;
            }
            const MaterialSlot& material_slot = material.material_slots[0];

            Scene::RenderData::Sprite3DRenderable renderable = {};
            renderable.instance_index = static_cast<uint32>(transform_array->entity_to_index[entity]);
            renderable.material_index = material.material_offset;
            renderable.world_position = math::GetPosition(transform_array->GetData(entity).world_transform);
            renderable.size = sprite.size;
            renderable.pivot = sprite.pivot;
            renderable.uv_rect = sprite.uv_rect;
            if (sprite.IsBillboard())
            {
                renderable.flags |= Scene::RenderData::Sprite3DRenderable::Billboard;
            }
            if ((material_slot.flags & SHADER_MATERIAL_FLAG_TRANSPARENT) != 0)
            {
                renderable.flags |= Scene::RenderData::Sprite3DRenderable::Transparent;
            }

            bucket.sprite_3d_renderables.push_back(renderable);
            sprite.SetDirty(false);
            });
        }
        jobsystem::Wait(sub_ctx);

        Size sprite_2d_renderable_count = 0;
        Size sprite_3d_renderable_count = 0;
        for (const SpriteBucket& bucket : sprite_2d_buckets)
        {
            sprite_2d_renderable_count += bucket.sprite_2d_renderables.size();
        }
        for (const SpriteBucket& bucket : sprite_3d_buckets)
        {
            sprite_3d_renderable_count += bucket.sprite_3d_renderables.size();
        }

        render_data.sprite_2d_renderables.reserve(sprite_2d_renderable_count);
        render_data.sprite_3d_renderables.reserve(sprite_3d_renderable_count);
        for (SpriteBucket& bucket : sprite_2d_buckets)
        {
            render_data.sprite_2d_renderables.insert(render_data.sprite_2d_renderables.end(), std::make_move_iterator(bucket.sprite_2d_renderables.begin()), std::make_move_iterator(bucket.sprite_2d_renderables.end()));
        }
        for (SpriteBucket& bucket : sprite_3d_buckets)
        {
            render_data.sprite_3d_renderables.insert(render_data.sprite_3d_renderables.end(), std::make_move_iterator(bucket.sprite_3d_renderables.begin()), std::make_move_iterator(bucket.sprite_3d_renderables.end()));
        }
    }
}
